#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace broker {

// ── Wire-level constants ──────────────────────────────────────────────────────

// All multi-byte fields are big-endian on the wire.
// Packet layout:
//   [0..1]  uint16  payload_length  (bytes that follow this header)
//   [2..3]  uint16  opcode
//   [4..]   bytes   payload

static constexpr uint16_t FLEXLM_HEADER_SIZE = 4; // length(2) + opcode(2)
static constexpr uint16_t FLEXLM_MAX_PACKET  = 8192;

// ── Opcodes ───────────────────────────────────────────────────────────────────

enum class Opcode : uint16_t {
    HELLO          = 0x0001,
    HELLO_ACK      = 0x0002,
    CHECKOUT       = 0x0010,
    CHECKOUT_ACK   = 0x0011,
    CHECKIN        = 0x0020,
    CHECKIN_ACK    = 0x0021,
    HEARTBEAT      = 0x0030,
    HEARTBEAT_ACK  = 0x0031,
    QUERY          = 0x0040,
    QUERY_ACK      = 0x0041,
    ERROR          = 0x00FF,
    UNKNOWN        = 0xFFFF
};

inline const char* opcode_name(Opcode op) {
    switch (op) {
    case Opcode::HELLO:         return "HELLO";
    case Opcode::HELLO_ACK:     return "HELLO_ACK";
    case Opcode::CHECKOUT:      return "CHECKOUT";
    case Opcode::CHECKOUT_ACK:  return "CHECKOUT_ACK";
    case Opcode::CHECKIN:       return "CHECKIN";
    case Opcode::CHECKIN_ACK:   return "CHECKIN_ACK";
    case Opcode::HEARTBEAT:     return "HEARTBEAT";
    case Opcode::HEARTBEAT_ACK: return "HEARTBEAT_ACK";
    case Opcode::QUERY:         return "QUERY";
    case Opcode::QUERY_ACK:     return "QUERY_ACK";
    case Opcode::ERROR:         return "ERROR";
    default:                    return "UNKNOWN";
    }
}

// ── Packet ────────────────────────────────────────────────────────────────────

struct Packet {
    Opcode               opcode{Opcode::UNKNOWN};
    std::vector<uint8_t> payload;

    // Total wire size including header
    size_t wire_size() const {
        return FLEXLM_HEADER_SIZE + payload.size();
    }
};

// ── Byte-order helpers ────────────────────────────────────────────────────────

inline uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}
inline uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         |  static_cast<uint32_t>(p[3]);
}
inline void write_u16_be(uint8_t* p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF;
    p[1] =  v       & 0xFF;
}
inline void write_u32_be(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] =  v        & 0xFF;
}

// ── String codec (uint8 length prefix + chars, no null terminator on wire) ───

inline std::string read_lstring(const uint8_t* p, size_t buf_len, size_t& offset) {
    if (offset >= buf_len)
        throw std::out_of_range("read_lstring: offset past end");
    uint8_t len = p[offset++];
    if (offset + len > buf_len)
        throw std::out_of_range("read_lstring: string extends past end");
    std::string s(reinterpret_cast<const char*>(p + offset), len);
    offset += len;
    return s;
}

inline void write_lstring(std::vector<uint8_t>& buf, const std::string& s) {
    if (s.size() > 255) throw std::invalid_argument("lstring too long (>255)");
    buf.push_back(static_cast<uint8_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}

// ── Structured message types ──────────────────────────────────────────────────

struct HelloMsg {
    uint32_t    client_version{0};
    std::string client_host;
    std::string username;

    static HelloMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct HelloAckMsg {
    uint32_t    server_version{0};
    std::string server_host;

    static HelloAckMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct CheckoutMsg {
    std::string feature;
    std::string version;
    std::string username;
    std::string client_host;
    std::string display;
    uint32_t    count{1};

    static CheckoutMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct CheckoutAckMsg {
    bool        granted{false};
    uint32_t    handle{0};      // license handle (used for CHECKIN)
    std::string denial_reason;  // non-empty when !granted

    static CheckoutAckMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct CheckinMsg {
    std::string feature;
    uint32_t    handle{0};

    static CheckinMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct CheckinAckMsg {
    bool ok{true};
    static CheckinAckMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct HeartbeatMsg {
    uint32_t seq{0};
    static HeartbeatMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct QueryMsg {
    std::string feature; // empty = query all features
    static QueryMsg decode(const Packet& pkt);
    Packet encode() const;
};

struct ErrorMsg {
    uint32_t    code{0};
    std::string message;
    static ErrorMsg decode(const Packet& pkt);
    Packet encode() const;
};

} // namespace broker
