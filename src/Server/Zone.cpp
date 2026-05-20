#include "Server/Zone.hpp"
#include "Core/Packet.hpp"
#include "Core/Opcodes.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

Zone::Zone() {
    // Platform layout in screen space (Y increases downward, origin top-left).
    // These are the authoritative footholds that are also sent to every client.
    footholds_ = {
        {{   0.0f, 600.0f }, { 800.0f, 600.0f }},  // main ground
        {{ 100.0f, 460.0f }, { 300.0f, 460.0f }},  // left ledge
        {{ 400.0f, 370.0f }, { 600.0f, 370.0f }},  // centre platform
        {{ 220.0f, 270.0f }, { 500.0f, 270.0f }},  // upper bridge
        {{ 580.0f, 490.0f }, { 750.0f, 490.0f }},  // right step
        {{  50.0f, 150.0f }, { 200.0f, 150.0f }},  // top-left perch
    };
}

Zone::~Zone() { stop(); }

void Zone::start() {
    running_ = true;
    thread_  = std::thread(&Zone::run, this);
}

void Zone::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// Public thread-safe interface
// ---------------------------------------------------------------------------

void Zone::pushTask(std::function<void()> task) {
    std::lock_guard lock(taskMutex_);
    taskQueue_.push(std::move(task));
}

void Zone::handleHello(int fd, uint8_t r, uint8_t g, uint8_t b) {
    pushTask([this, fd, r, g, b]() { addEntity_(fd, r, g, b); });
}

void Zone::handleMove(int fd, float x, float y, float vx, float vy) {
    pushTask([this, fd, x, y, vx, vy]() { updateMove_(fd, x, y, vx, vy); });
}

void Zone::handleDisconnect(int fd) {
    pushTask([this, fd]() { removeEntity_(fd); });
}

// ---------------------------------------------------------------------------
// Zone thread main loop
// ---------------------------------------------------------------------------

void Zone::run() {
    using Clock = std::chrono::steady_clock;
    auto lastTick = Clock::now();

    while (running_) {
        auto now = Clock::now();
        float dt = std::chrono::duration<float>(now - lastTick).count();

        if (dt >= TICK_RATE) {
            lastTick = now;

            // Drain task queue into a local copy to minimise lock hold time.
            std::queue<std::function<void()>> local;
            {
                std::lock_guard lock(taskMutex_);
                std::swap(local, taskQueue_);
            }
            while (!local.empty()) {
                local.front()();
                local.pop();
            }

            tick(TICK_RATE);
        } else {
            // Sleep for half the remaining slice to avoid busy-spinning.
            auto usLeft = static_cast<long>((TICK_RATE - dt) * 1e6f * 0.5f);
            std::this_thread::sleep_for(std::chrono::microseconds(usLeft));
        }
    }
}

void Zone::tick(float dt) {
    for (auto& [id, e] : entities_)
        applyPhysics_(e, dt);

    broadcastReplicate_();
}

// ---------------------------------------------------------------------------
// Physics
// ---------------------------------------------------------------------------

void Zone::applyPhysics_(Entity& e, float dt) {
    if (!e.onGround)
        e.vy += GRAVITY * dt;

    float newX = e.x + e.vx * dt;
    float newY = e.y + e.vy * dt;

    // World boundary clamp (horizontal).
    newX = std::clamp(newX, 0.0f, 800.0f - ENTITY_SIZE);

    // Foothold collision: check entity's bottom edge against each platform.
    float oldBottom = e.y + ENTITY_SIZE;
    float newBottom = newY + ENTITY_SIZE;
    e.onGround = false;

    for (const auto& fh : footholds_) {
        if (!inFootholdXRange(fh, newX + ENTITY_SIZE * 0.5f)) continue;
        float fhY = footholdYAt(fh, newX + ENTITY_SIZE * 0.5f);

        // Crossed the foothold from above → snap.
        if (oldBottom <= fhY + 1.0f && newBottom >= fhY) {
            newY       = fhY - ENTITY_SIZE;
            e.vy       = 0.0f;
            e.onGround = true;
            break;
        }
    }

    if (newX != e.x || newY != e.y)
        e.dirty = true;

    e.x = newX;
    e.y = newY;
}

// ---------------------------------------------------------------------------
// Entity management (zone thread only)
// ---------------------------------------------------------------------------

void Zone::addEntity_(int fd, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t id = nextId_++;
    Entity e;
    e.id    = id;
    e.fd    = fd;
    e.x     = 80.0f + ((id - 1) % 7) * 100.0f;
    e.y     = 50.0f;
    e.color = {r, g, b};

    // 1. Send ServerAssignId to the new client (footholds + spawn position).
    {
        PacketWriter pw(toU16(Opcode::ServerAssignId));
        pw.writeU32(id);
        pw.writeU8(r); pw.writeU8(g); pw.writeU8(b);
        pw.writeFloat(e.x); pw.writeFloat(e.y);
        pw.writeU32(static_cast<uint32_t>(footholds_.size()));
        for (const auto& fh : footholds_) {
            pw.writeFloat(fh.p1.x); pw.writeFloat(fh.p1.y);
            pw.writeFloat(fh.p2.x); pw.writeFloat(fh.p2.y);
        }
        sendTo_(fd, pw.finalize());
    }

    // 2. Inform the new client of every already-present entity.
    for (const auto& [eid, ex] : entities_) {
        PacketWriter pw(toU16(Opcode::ServerJoin));
        pw.writeU32(eid);
        pw.writeU8(ex.color.r); pw.writeU8(ex.color.g); pw.writeU8(ex.color.b);
        pw.writeFloat(ex.x); pw.writeFloat(ex.y);
        sendTo_(fd, pw.finalize());
    }

    // 3. Register.
    entities_[id] = e;
    fdToId_[fd]   = id;

    // 4. Announce the new entity to everyone (including itself — client ignores own join).
    {
        PacketWriter pw(toU16(Opcode::ServerJoin));
        pw.writeU32(id);
        pw.writeU8(r); pw.writeU8(g); pw.writeU8(b);
        pw.writeFloat(e.x); pw.writeFloat(e.y);
        broadcastAll_(pw.finalize());
    }

    std::cout << "[Zone] Entity " << id << " joined (fd=" << fd
              << " rgb=(" << (int)r << ',' << (int)g << ',' << (int)b << "))\n";
}

void Zone::removeEntity_(int fd) {
    auto it = fdToId_.find(fd);
    if (it == fdToId_.end()) return;

    uint32_t id = it->second;
    fdToId_.erase(it);
    entities_.erase(id);

    PacketWriter pw(toU16(Opcode::ServerLeave));
    pw.writeU32(id);
    broadcastAll_(pw.finalize());

    std::cout << "[Zone] Entity " << id << " left (fd=" << fd << ")\n";
}

void Zone::updateMove_(int fd, float x, float y, float vx, float vy) {
    auto it = fdToId_.find(fd);
    if (it == fdToId_.end()) return;

    Entity* e = [&]() -> Entity* {
        auto eit = entities_.find(it->second);
        return eit != entities_.end() ? &eit->second : nullptr;
    }();
    if (!e) return;

    // Speed anti-cheat: reject teleports beyond max legal displacement per tick.
    float dx   = x - e->x;
    float dy   = y - e->y;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist > MAX_SPEED * TICK_RATE * 10.0f) {
        PacketWriter pw(toU16(Opcode::ServerCorrect));
        pw.writeFloat(e->x);
        pw.writeFloat(e->y);
        sendTo_(fd, pw.finalize());
        std::cout << "[Zone] Corrected entity " << e->id
                  << " (delta=" << dist << " px)\n";
        return;
    }

    e->x     = x;
    e->y     = y;
    e->vx    = vx;
    e->vy    = vy;
    e->dirty = true;
}

// ---------------------------------------------------------------------------
// Broadcasting (zone thread only)
// ---------------------------------------------------------------------------

void Zone::broadcastReplicate_() {
    // Collect only entities whose state changed this tick.
    std::vector<const Entity*> changed;
    for (const auto& [id, e] : entities_)
        if (e.dirty) changed.push_back(&e);

    if (changed.empty()) return;

    PacketWriter pw(toU16(Opcode::ServerReplicate));
    pw.writeU32(static_cast<uint32_t>(changed.size()));
    for (const Entity* e : changed) {
        pw.writeU32(e->id);
        pw.writeFloat(e->x);
        pw.writeFloat(e->y);
        pw.writeFloat(e->vx);
        pw.writeFloat(e->vy);
    }
    broadcastAll_(pw.finalize());

    // Clear dirty flags after the broadcast.
    for (auto& [id, e] : entities_)
        e.dirty = false;
}

void Zone::broadcastAll_(const std::vector<uint8_t>& pkt) {
    for (const auto& [id, e] : entities_)
        sendTo_(e.fd, pkt);
}

void Zone::sendTo_(int fd, const std::vector<uint8_t>& pkt) {
    if (onSend) onSend(fd, pkt);
}
