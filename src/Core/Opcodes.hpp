#pragma once
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iomanip>

enum class Opcode : uint16_t {
    // Client -> Server
    ClientHello    = 0x0001,  // [u8 r, u8 g, u8 b]
    ClientMove     = 0x0002,  // [f32 x, f32 y, f32 vx, f32 vy]

    // Server -> Client
    ServerAssignId = 0x0010,  // [u32 id, u8 r,g,b, f32 x,y, u32 numFH, FH...]
    ServerReplicate= 0x0011,  // [u32 count, (u32 id, f32 x,y,vx,vy) * count]
    ServerCorrect  = 0x0012,  // [f32 x, f32 y]
    ServerJoin     = 0x0013,  // [u32 id, u8 r,g,b, f32 x,y]
    ServerLeave    = 0x0014,  // [u32 id]
};

inline uint16_t toU16(Opcode op) {
    return static_cast<uint16_t>(op);
}

inline const char* opcodeName(uint16_t op) {
    switch (op) {
        case 0x0001: return "ClientHello";
        case 0x0002: return "ClientMove";
        case 0x0010: return "ServerAssignId";
        case 0x0011: return "ServerReplicate";
        case 0x0012: return "ServerCorrect";
        case 0x0013: return "ServerJoin";
        case 0x0014: return "ServerLeave";
        default:     return "Unknown";
    }
}

// Reads opcode+length from raw packet bytes and prints a log line.
// tag  : "[SERVER RECV]", "[CLIENT SEND]", etc.
// fd   : pass -1 to omit.
inline void logPkt(const char* tag, const uint8_t* data, size_t size, int fd = -1) {
    if (size < 4) return;
    uint16_t len = 0, op = 0;
    std::memcpy(&len, data,     2);
    std::memcpy(&op,  data + 2, 2);
    std::cout << tag;
    if (fd >= 0) std::cout << " fd=" << fd;
    std::cout << " | " << opcodeName(op)
              << " (0x" << std::hex << std::setw(4) << std::setfill('0') << op << std::dec << ")"
              << " " << len << "B\n";
}
