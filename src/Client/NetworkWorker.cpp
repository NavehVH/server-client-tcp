#include "Client/NetworkWorker.hpp"
#include "Core/Opcodes.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <chrono>

NetworkWorker::NetworkWorker(std::string host, uint16_t port)
    : host_(std::move(host)), port_(port) {}

NetworkWorker::~NetworkWorker() { stop(); }

void NetworkWorker::start() {
    running_ = true;
    thread_  = std::thread(&NetworkWorker::run, this);
}

void NetworkWorker::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
}

void NetworkWorker::send(std::vector<uint8_t> pkt) {
    std::lock_guard lock(outMutex_);
    outQueue_.push(std::move(pkt));
}

std::optional<std::vector<uint8_t>> NetworkWorker::pollPacket() {
    std::lock_guard lock(inMutex_);
    if (inQueue_.empty()) return std::nullopt;
    auto pkt = std::move(inQueue_.front());
    inQueue_.pop();
    return pkt;
}

// ---------------------------------------------------------------------------
// Worker thread
// ---------------------------------------------------------------------------

bool NetworkWorker::connectSocket() {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return false;

    int yes = 1;
    setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        close(fd_); fd_ = -1;
        return false;
    }

    if (connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd_); fd_ = -1;
        return false;
    }

    return true;
}

void NetworkWorker::flushOutgoing() {
    std::queue<std::vector<uint8_t>> local;
    {
        std::lock_guard lock(outMutex_);
        std::swap(local, outQueue_);
    }
    while (!local.empty()) {
        const auto& pkt   = local.front();
        logPkt("[CLIENT SEND]", pkt.data(), pkt.size());
        ssize_t     total = static_cast<ssize_t>(pkt.size());
        ssize_t     sent  = 0;
        while (sent < total) {
            ssize_t n = ::send(fd_, pkt.data() + sent,
                               static_cast<size_t>(total - sent), MSG_NOSIGNAL);
            if (n <= 0) return;
            sent += n;
        }
        local.pop();
    }
}

void NetworkWorker::run() {
    // Retry connection with back-off until stopped.
    while (running_) {
        std::cout << "[Net] Connecting to " << host_ << ":" << port_ << "...\n";
        if (!connectSocket()) {
            std::cout << "[Net] Connection failed; retrying in 2s\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        connected_ = true;
        std::cout << "[Net] Connected (fd=" << fd_ << ")\n";
        recvBuf_.clear();

        uint8_t tmp[4096];
        while (running_) {
            // Send queued outgoing packets.
            flushOutgoing();

            // Non-blocking read.
            ssize_t n = recv(fd_, tmp, sizeof(tmp), MSG_DONTWAIT);
            if (n > 0) {
                recvBuf_.append(tmp, static_cast<size_t>(n));
                while (auto pkt = recvBuf_.tryExtract()) {
                    logPkt("[CLIENT RECV]", pkt->data(), pkt->size());
                    std::lock_guard lock(inMutex_);
                    inQueue_.push(std::move(*pkt));
                }
            } else if (n == 0) {
                std::cout << "[Net] Server closed connection\n";
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Nothing ready — yield for ~1ms to avoid spinning.
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                } else {
                    std::cout << "[Net] recv error: " << strerror(errno) << "\n";
                    break;
                }
            }
        }

        connected_ = false;
        close(fd_); fd_ = -1;

        if (running_) {
            std::cout << "[Net] Disconnected; retrying in 2s\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
}
