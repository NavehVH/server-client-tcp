#include "Server/NetworkLoop.hpp"
#include "Core/Opcodes.hpp"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void makeNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void setTcpNoDelay(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

NetworkLoop::NetworkLoop(uint16_t port) : port_(port) {}

NetworkLoop::~NetworkLoop() { stop(); }

void NetworkLoop::start() {
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) throw std::runtime_error("socket() failed");

    int yes = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setTcpNoDelay(serverFd_);
    makeNonBlocking(serverFd_);

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed on port " + std::to_string(port_));
    if (listen(serverFd_, 64) < 0)
        throw std::runtime_error("listen() failed");

    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) throw std::runtime_error("epoll_create1() failed");

    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = serverFd_;
    epoll_ctl(epollFd_, EPOLL_CTL_ADD, serverFd_, &ev);

    running_ = true;
    thread_  = std::thread(&NetworkLoop::run, this);
    std::cout << "[Net] Listening on 0.0.0.0:" << port_ << "\n";
}

void NetworkLoop::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (epollFd_  >= 0) { close(epollFd_);  epollFd_  = -1; }
    if (serverFd_ >= 0) { close(serverFd_); serverFd_ = -1; }
}

// ---------------------------------------------------------------------------
// I/O loop (edge-triggered epoll)
// ---------------------------------------------------------------------------

void NetworkLoop::run() {
    constexpr int MAX_EV = 64;
    epoll_event   events[MAX_EV];

    while (running_) {
        int n = epoll_wait(epollFd_, events, MAX_EV, 10 /*ms*/);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == serverFd_) {
                acceptClient();
            } else if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                closeClient(fd);
            } else if (events[i].events & EPOLLIN) {
                handleRead(fd);
            }
        }
    }
}

void NetworkLoop::acceptClient() {
    sockaddr_in caddr{};
    socklen_t   clen = sizeof(caddr);

    while (true) {
        int cfd = accept(serverFd_, reinterpret_cast<sockaddr*>(&caddr), &clen);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;  // unexpected error; stop accepting this batch
        }

        makeNonBlocking(cfd);
        setTcpNoDelay(cfd);

        // Edge-triggered: we must drain fully when data arrives.
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = cfd;
        epoll_ctl(epollFd_, EPOLL_CTL_ADD, cfd, &ev);

        sessions_.emplace(cfd, Session{cfd, {}});
        std::cout << "[Net] Client connected fd=" << cfd << "\n";
        if (onConnect) onConnect(cfd);
    }
}

void NetworkLoop::handleRead(int fd) {
    auto it = sessions_.find(fd);
    if (it == sessions_.end()) return;
    Session& session = it->second;

    uint8_t tmp[4096];
    while (true) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) {
            session.recvBuf.append(tmp, static_cast<size_t>(n));
            while (auto pkt = session.recvBuf.tryExtract()) {
                logPkt("[SERVER RECV]", pkt->data(), pkt->size(), fd);
                if (onPacket)
                    onPacket(fd, pkt->data(), pkt->size());
            }
        } else if (n == 0) {
            closeClient(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeClient(fd);
            return;
        }
    }
}

void NetworkLoop::closeClient(int fd) {
    std::cout << "[Net] Client disconnected fd=" << fd << "\n";
    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    sessions_.erase(fd);
    if (onDisconnect) onDisconnect(fd);
}

// ---------------------------------------------------------------------------
// Send (safe to call from any thread — send() is reentrant)
// ---------------------------------------------------------------------------

void NetworkLoop::sendTo(int fd, const std::vector<uint8_t>& data) {
    logPkt("[SERVER SEND]", data.data(), data.size(), fd);
    const uint8_t* ptr   = data.data();
    ssize_t        total = static_cast<ssize_t>(data.size());
    ssize_t        sent  = 0;

    while (sent < total) {
        ssize_t n = ::send(fd, ptr + sent, static_cast<size_t>(total - sent), MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += n;
    }
}
