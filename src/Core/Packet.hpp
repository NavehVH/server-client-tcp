#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <optional>

// Binary packet framing (all values little-endian):
//   Bytes 0-1 : uint16_t totalLength  (includes the 4-byte header itself)
//   Bytes 2-3 : uint16_t opcode
//   Bytes 4+  : payload

static constexpr size_t PACKET_HEADER_SIZE = 4;

// ---------------------------------------------------------------------------
// PacketWriter — builds an outgoing binary packet.
// ---------------------------------------------------------------------------
class PacketWriter {
public:
    explicit PacketWriter(uint16_t opcode) {
        buf_.resize(PACKET_HEADER_SIZE);
        std::memcpy(buf_.data() + 2, &opcode, 2);
    }

    void writeU8(uint8_t v)    { buf_.push_back(v); }
    void writeU16(uint16_t v)  { append(&v, 2); }
    void writeU32(uint32_t v)  { append(&v, 4); }
    void writeFloat(float v)   { append(&v, 4); }

    // Stamps the length field and returns the complete byte buffer.
    std::vector<uint8_t> finalize() {
        uint16_t len = static_cast<uint16_t>(buf_.size());
        std::memcpy(buf_.data(), &len, 2);
        return buf_;
    }

private:
    void append(const void* src, size_t n) {
        auto* p = static_cast<const uint8_t*>(src);
        buf_.insert(buf_.end(), p, p + n);
    }
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// PacketReader — reads fields from an incoming binary packet.
// ---------------------------------------------------------------------------
class PacketReader {
public:
    PacketReader(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(PACKET_HEADER_SIZE) {}

    uint16_t opcode() const {
        uint16_t op = 0;
        std::memcpy(&op, data_ + 2, 2);
        return op;
    }

    uint8_t  readU8()    { uint8_t  v = 0; read(&v, 1); return v; }
    uint16_t readU16()   { uint16_t v = 0; read(&v, 2); return v; }
    uint32_t readU32()   { uint32_t v = 0; read(&v, 4); return v; }
    float    readFloat() { float    v = 0; read(&v, 4); return v; }

    bool ok() const { return pos_ <= size_; }

private:
    void read(void* dst, size_t n) {
        if (pos_ + n > size_) return;
        std::memcpy(dst, data_ + pos_, n);
        pos_ += n;
    }
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

// ---------------------------------------------------------------------------
// StreamBuffer — accumulates raw TCP bytes and extracts framed packets.
// Handles stream fragmentation safely.
// ---------------------------------------------------------------------------
class StreamBuffer {
public:
    void append(const uint8_t* data, size_t n) {
        buf_.insert(buf_.end(), data, data + n);
    }

    // Returns the next fully-framed packet bytes, or empty if more data needed.
    std::optional<std::vector<uint8_t>> tryExtract() {
        if (buf_.size() < PACKET_HEADER_SIZE) return std::nullopt;

        uint16_t len = 0;
        std::memcpy(&len, buf_.data(), 2);

        if (len < PACKET_HEADER_SIZE) {
            buf_.clear();  // malformed; discard session buffer
            return std::nullopt;
        }
        if (buf_.size() < len) return std::nullopt;

        std::vector<uint8_t> pkt(buf_.begin(), buf_.begin() + len);
        buf_.erase(buf_.begin(), buf_.begin() + len);
        return pkt;
    }

    void clear() { buf_.clear(); }

private:
    std::vector<uint8_t> buf_;
};
