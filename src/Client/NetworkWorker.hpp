#pragma once
#include "Core/Packet.hpp"
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <optional>

// Background thread that owns the TCP socket to the server.
// The render thread calls send() and pollPacket(); the worker thread
// handles all blocking I/O without stalling the Raylib window loop.
class NetworkWorker {
public:
    NetworkWorker(std::string host, uint16_t port);
    ~NetworkWorker();

    void start();
    void stop();

    bool isConnected() const { return connected_; }

    // Thread-safe: enqueue an outgoing packet (called from render thread).
    void send(std::vector<uint8_t> pkt);

    // Thread-safe: dequeue the next completed incoming packet, or nullopt.
    std::optional<std::vector<uint8_t>> pollPacket();

private:
    void run();
    bool connectSocket();
    void flushOutgoing();

    std::string host_;
    uint16_t    port_;
    int         fd_ = -1;

    StreamBuffer recvBuf_;

    std::queue<std::vector<uint8_t>> outQueue_;
    std::mutex                       outMutex_;

    std::queue<std::vector<uint8_t>> inQueue_;
    std::mutex                       inMutex_;

    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
};
