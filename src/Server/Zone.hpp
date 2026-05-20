#pragma once
#include "Core/Types.hpp"
#include "Server/Entity.hpp"
#include <vector>
#include <queue>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <unordered_map>

class Zone {
public:
    Zone();
    ~Zone();

    void start();
    void stop();

    // Thread-safe — push a callable onto the zone thread's task queue.
    void pushTask(std::function<void()> task);

    // Convenience wrappers that push tasks internally (safe to call from any thread).
    void handleHello(int fd, uint8_t r, uint8_t g, uint8_t b);
    void handleMove(int fd, float x, float y, float vx, float vy);
    void handleDisconnect(int fd);

    const std::vector<Foothold>& footholds() const { return footholds_; }

    // Wired by Main before start().
    std::function<void(int fd, std::vector<uint8_t>)> onSend;

    static constexpr float TICK_RATE  = 1.0f / 60.0f;
    static constexpr float GRAVITY    = 980.0f;
    static constexpr float MAX_SPEED  = 600.0f;  // pixels per second

private:
    // --- Zone-thread-only methods (no locking required on game state) ---
    void run();
    void tick(float dt);
    void applyPhysics_(Entity& e, float dt);
    void broadcastReplicate_();

    void addEntity_(int fd, uint8_t r, uint8_t g, uint8_t b);
    void removeEntity_(int fd);
    void updateMove_(int fd, float x, float y, float vx, float vy);

    void broadcastAll_(const std::vector<uint8_t>& pkt);
    void sendTo_(int fd, const std::vector<uint8_t>& pkt);

    // --- Game state (only touched from zone thread) ---
    std::vector<Foothold>              footholds_;
    std::unordered_map<uint32_t, Entity> entities_;  // entityId -> Entity
    std::unordered_map<int, uint32_t>    fdToId_;    // socket fd -> entityId
    uint32_t nextId_ = 1;

    // --- Task queue (shared between threads) ---
    std::queue<std::function<void()>> taskQueue_;
    std::mutex                        taskMutex_;

    std::thread        thread_;
    std::atomic<bool>  running_{false};
};
