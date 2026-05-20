#include "Client/NetworkWorker.hpp"
#include "Client/Physics.hpp"
#include "Core/Packet.hpp"
#include "Core/Opcodes.hpp"
#include "Core/Types.hpp"
#include <raylib.h>
#include <iostream>
#include <unordered_map>
#include <string>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// Remote entity state with linear interpolation between server updates.
// ---------------------------------------------------------------------------
struct RemoteEntity {
    uint32_t id;
    Color3   color;

    float prevX, prevY;    // position at the start of the last interpolation window
    float targX, targY;    // target position from the latest server tick
    float lerpT = 1.0f;    // 0.0 → prev, 1.0 → targ

    float curX() const { return prevX + (targX - prevX) * lerpT; }
    float curY() const { return prevY + (targY - prevY) * lerpT; }
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static constexpr float ENTITY_SIZE    = 32.0f;
static constexpr float LERP_DURATION  = 1.0f / 20.0f;  // catch up over 50 ms
static constexpr int   SEND_EVERY_N   = 3;              // send position every N frames

static uint32_t                             localId = 0;
static bool                                 assigned = false;
static Color3                               localColor{};
static Physics                              physics;
static std::vector<Foothold>                footholds;
static std::unordered_map<uint32_t, RemoteEntity> remotes;

// ---------------------------------------------------------------------------
// Packet dispatch
// ---------------------------------------------------------------------------
static void processPacket(const std::vector<uint8_t>& raw) {
    PacketReader pr(raw.data(), raw.size());
    auto opcode = static_cast<Opcode>(pr.opcode());

    switch (opcode) {

    case Opcode::ServerAssignId: {
        localId        = pr.readU32();
        localColor.r   = pr.readU8();
        localColor.g   = pr.readU8();
        localColor.b   = pr.readU8();
        physics.x      = pr.readFloat();
        physics.y      = pr.readFloat();
        uint32_t numFH = pr.readU32();
        footholds.clear();
        for (uint32_t i = 0; i < numFH; ++i) {
            Foothold fh;
            fh.p1.x = pr.readFloat(); fh.p1.y = pr.readFloat();
            fh.p2.x = pr.readFloat(); fh.p2.y = pr.readFloat();
            footholds.push_back(fh);
        }
        assigned = true;
        std::cout << "[Client] Assigned id=" << localId
                  << "  footholds=" << footholds.size() << "\n";
        break;
    }

    case Opcode::ServerJoin: {
        uint32_t id = pr.readU32();
        if (id == localId) break;  // ignore our own announcement
        RemoteEntity re;
        re.id      = id;
        re.color.r = pr.readU8();
        re.color.g = pr.readU8();
        re.color.b = pr.readU8();
        re.prevX = re.targX = pr.readFloat();
        re.prevY = re.targY = pr.readFloat();
        re.lerpT = 1.0f;
        remotes[id] = re;
        std::cout << "[Client] Entity " << id << " joined\n";
        break;
    }

    case Opcode::ServerLeave: {
        uint32_t id = pr.readU32();
        remotes.erase(id);
        std::cout << "[Client] Entity " << id << " left\n";
        break;
    }

    case Opcode::ServerReplicate: {
        uint32_t count = pr.readU32();
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t id = pr.readU32();
            float nx    = pr.readFloat();
            float ny    = pr.readFloat();
            pr.readFloat();  // vx (unused by client renderer)
            pr.readFloat();  // vy (unused by client renderer)

            if (id == localId) continue;  // server authoritative position for self comes via Correct

            auto it = remotes.find(id);
            if (it == remotes.end()) {
                // Late-join: insert a placeholder.
                RemoteEntity re;
                re.id = id; re.color = {200, 200, 200};
                re.prevX = re.targX = nx; re.prevY = re.targY = ny;
                re.lerpT = 1.0f;
                remotes[id] = re;
            } else {
                RemoteEntity& re = it->second;
                re.prevX = re.curX();
                re.prevY = re.curY();
                re.targX = nx;
                re.targY = ny;
                re.lerpT = 0.0f;
            }
        }
        break;
    }

    case Opcode::ServerCorrect: {
        float cx = pr.readFloat();
        float cy = pr.readFloat();
        physics.snapTo(cx, cy);
        std::cout << "[Client] Position corrected to (" << cx << ", " << cy << ")\n";
        break;
    }

    default:
        std::cout << "[Client] Unknown opcode 0x" << std::hex << pr.opcode()
                  << std::dec << "\n";
    }
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------
static Color toRayColor(Color3 c, unsigned char a = 255) {
    return Color{c.r, c.g, c.b, a};
}

static void drawEntity(float x, float y, Color3 c, const char* label = nullptr) {
    DrawRectangle(static_cast<int>(x), static_cast<int>(y),
                  static_cast<int>(ENTITY_SIZE), static_cast<int>(ENTITY_SIZE),
                  toRayColor(c));
    DrawRectangleLines(static_cast<int>(x), static_cast<int>(y),
                       static_cast<int>(ENTITY_SIZE), static_cast<int>(ENTITY_SIZE),
                       WHITE);
    if (label)
        DrawText(label, static_cast<int>(x), static_cast<int>(y) - 14, 10, WHITE);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    uint16_t    port = (argc > 2) ? static_cast<uint16_t>(std::stoi(argv[2])) : 7777;

    // Pick a random pastel color so players are visually distinct.
    srand(static_cast<unsigned>(time(nullptr)));
    Color3 myColor{
        static_cast<uint8_t>(100 + rand() % 155),
        static_cast<uint8_t>(100 + rand() % 155),
        static_cast<uint8_t>(100 + rand() % 155)
    };

    NetworkWorker net(host, port);
    net.start();

    InitWindow(800, 600, "server-client-tcp — client");
    SetTargetFPS(60);

    bool  helloSent  = false;
    int   frameCount = 0;
    bool  prevJump   = false;
    float lastSentX = -1e9f, lastSentY = -1e9f;
    float lastSentVx = 0.0f, lastSentVy = 0.0f;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        ++frameCount;

        // --- Send ClientHello once the socket is up ---
        if (!helloSent && net.isConnected()) {
            PacketWriter pw(toU16(Opcode::ClientHello));
            pw.writeU8(myColor.r); pw.writeU8(myColor.g); pw.writeU8(myColor.b);
            net.send(pw.finalize());
            helloSent = true;
            std::cout << "[Client] Sent ClientHello\n";
        }

        // If socket dropped and reconnected, re-send hello.
        if (helloSent && !net.isConnected()) {
            helloSent = false;
            assigned  = false;
            remotes.clear();
            footholds.clear();
        }

        // --- Drain incoming packets ---
        while (auto pkt = net.pollPacket())
            processPacket(*pkt);

        // --- Advance remote entity interpolation ---
        for (auto& [id, re] : remotes) {
            re.lerpT = std::min(1.0f, re.lerpT + dt / LERP_DURATION);
        }

        // --- Local physics (only after assignment) ---
        if (assigned) {
            bool jumpDown = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_UP) || IsKeyDown(KEY_W);
            physics.moveLeft   = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
            physics.moveRight  = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
            physics.jumpPressed = jumpDown && !prevJump;
            prevJump = jumpDown;

            physics.update(dt, footholds);

            // Send only when position or velocity changed, throttled to every N frames.
            if (frameCount % SEND_EVERY_N == 0) {
                float dx  = physics.x  - lastSentX;
                float dy  = physics.y  - lastSentY;
                float dvx = physics.vx - lastSentVx;
                float dvy = physics.vy - lastSentVy;
                bool posChanged = (dx*dx + dy*dy)   > 0.01f;
                bool velChanged = (dvx*dvx + dvy*dvy) > 0.01f;
                if (posChanged || velChanged) {
                    PacketWriter pw(toU16(Opcode::ClientMove));
                    pw.writeFloat(physics.x);
                    pw.writeFloat(physics.y);
                    pw.writeFloat(physics.vx);
                    pw.writeFloat(physics.vy);
                    net.send(pw.finalize());
                    lastSentX  = physics.x;  lastSentY  = physics.y;
                    lastSentVx = physics.vx; lastSentVy = physics.vy;
                }
            }
        }

        // --- Draw ---
        BeginDrawing();
        ClearBackground(Color{20, 20, 30, 255});

        // Footholds
        for (const auto& fh : footholds) {
            DrawLineEx(
                Vector2{fh.p1.x, fh.p1.y},
                Vector2{fh.p2.x, fh.p2.y},
                3.0f, Color{80, 180, 80, 255}
            );
        }

        // Remote entities (interpolated)
        for (const auto& [id, re] : remotes) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "#%u", id);
            drawEntity(re.curX(), re.curY(), re.color, lbl);
        }

        // Local player
        if (assigned) {
            char lbl[16];
            snprintf(lbl, sizeof(lbl), "me #%u", localId);
            drawEntity(physics.x, physics.y, myColor, lbl);
        }

        // HUD
        if (!net.isConnected())
            DrawText("Connecting to server...", 10, 10, 18, YELLOW);
        else if (!assigned)
            DrawText("Waiting for server assignment...", 10, 10, 18, YELLOW);
        else {
            DrawText(TextFormat("x=%.1f y=%.1f  vx=%.1f vy=%.1f  %s",
                physics.x, physics.y, physics.vx, physics.vy,
                physics.onGround ? "GROUNDED" : "AIRBORNE"),
                10, 10, 14, LIGHTGRAY);
            DrawText("WASD / Arrows to move  |  Space / W / Up to jump",
                10, 580, 12, DARKGRAY);
        }

        DrawFPS(10, 30);
        EndDrawing();
    }

    net.stop();
    CloseWindow();
    return 0;
}
