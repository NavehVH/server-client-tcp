#include "Server/NetworkLoop.hpp"
#include "Server/Zone.hpp"
#include "Core/Packet.hpp"
#include "Core/Opcodes.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

static std::atomic<bool> gRunning{true};

int main() {
    std::signal(SIGINT,  [](int) { gRunning = false; });
    std::signal(SIGPIPE, SIG_IGN);  // ignore broken-pipe on dead sockets

    Zone        zone;
    NetworkLoop net(7777);

    // Zone sends packets out via NetworkLoop.
    zone.onSend = [&net](int fd, std::vector<uint8_t> pkt) {
        net.sendTo(fd, pkt);
    };

    // A new TCP connection was accepted — wait for ClientHello before creating an entity.
    net.onConnect = [](int fd) {
        std::cout << "[Server] New connection fd=" << fd << "; awaiting ClientHello\n";
    };

    // Connection closed — remove the associated entity from the zone.
    net.onDisconnect = [&zone](int fd) {
        zone.handleDisconnect(fd);
    };

    // Packet received — decode and dispatch to the zone.
    net.onPacket = [&zone](int fd, const uint8_t* data, size_t len) {
        PacketReader pr(data, len);
        auto opcode = static_cast<Opcode>(pr.opcode());

        switch (opcode) {
            case Opcode::ClientHello: {
                uint8_t r = pr.readU8();
                uint8_t g = pr.readU8();
                uint8_t b = pr.readU8();
                std::cout << "[Server] ClientHello fd=" << fd
                          << " rgb=(" << (int)r << ',' << (int)g << ',' << (int)b << ")\n";
                zone.handleHello(fd, r, g, b);
                break;
            }
            case Opcode::ClientMove: {
                float x  = pr.readFloat();
                float y  = pr.readFloat();
                float vx = pr.readFloat();
                float vy = pr.readFloat();
                zone.handleMove(fd, x, y, vx, vy);
                break;
            }
            default:
                std::cout << "[Server] Unknown opcode 0x"
                          << std::hex << pr.opcode() << std::dec
                          << " from fd=" << fd << "\n";
        }
    };

    zone.start();
    net.start();

    std::cout << "[Server] server-client-tcp running. Ctrl+C to quit.\n";
    while (gRunning)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "[Server] Shutting down...\n";
    net.stop();
    zone.stop();
    std::cout << "[Server] Bye.\n";
}
