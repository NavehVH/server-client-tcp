#pragma once
#include "Core/Packet.hpp"
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <thread>

struct Session {
    int          fd;
    StreamBuffer recvBuf;
};

class NetworkLoop {
public:
    explicit NetworkLoop(uint16_t port);
    ~NetworkLoop();

    void start();
    void stop();

    // Callbacks — set before start().
    std::function<void(int fd)>                                  onConnect;
    std::function<void(int fd)>                                  onDisconnect;
    std::function<void(int fd, const uint8_t* data, size_t len)> onPacket;

    // Thread-safe: may be called from any thread.
    void sendTo(int fd, const std::vector<uint8_t>& data);

private:
    void run();
    void acceptClient();
    void handleRead(int fd);
    void closeClient(int fd);

    uint16_t port_;
    int      epollFd_  = -1;
    int      serverFd_ = -1;

    std::unordered_map<int, Session> sessions_;
    std::thread                      thread_;
    std::atomic<bool>                running_{false};
};
