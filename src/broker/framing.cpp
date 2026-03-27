#include "broker/framing.h"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace broker {

// ── FrameReader ───────────────────────────────────────────────────────────────

void FrameReader::push(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
}

std::optional<Packet> FrameReader::pop() {
    if (error_) return std::nullopt;

    // Need at least the 4-byte header
    if (buf_.size() < FLEXLM_HEADER_SIZE) return std::nullopt;

    uint16_t payload_len = read_u16_be(buf_.data());      // bytes after header
    uint16_t raw_opcode  = read_u16_be(buf_.data() + 2);

    // Sanity-check length before allocating
    if (payload_len > FLEXLM_MAX_PACKET) {
        spdlog::error("[framing] Oversized packet (len={}), closing connection",
                      payload_len);
        error_ = true;
        return std::nullopt;
    }

    size_t total = FLEXLM_HEADER_SIZE + payload_len;
    if (buf_.size() < total) return std::nullopt; // need more bytes

    Packet pkt;
    // Map raw opcode — treat anything unrecognised as UNKNOWN
    switch (raw_opcode) {
    case 0x0001: pkt.opcode = Opcode::HELLO;         break;
    case 0x0002: pkt.opcode = Opcode::HELLO_ACK;     break;
    case 0x0010: pkt.opcode = Opcode::CHECKOUT;      break;
    case 0x0011: pkt.opcode = Opcode::CHECKOUT_ACK;  break;
    case 0x0020: pkt.opcode = Opcode::CHECKIN;       break;
    case 0x0021: pkt.opcode = Opcode::CHECKIN_ACK;   break;
    case 0x0030: pkt.opcode = Opcode::HEARTBEAT;     break;
    case 0x0031: pkt.opcode = Opcode::HEARTBEAT_ACK; break;
    case 0x0040: pkt.opcode = Opcode::QUERY;         break;
    case 0x0041: pkt.opcode = Opcode::QUERY_ACK;     break;
    case 0x00FF: pkt.opcode = Opcode::ERROR;         break;
    default:     pkt.opcode = Opcode::UNKNOWN;       break;
    }

    pkt.payload.assign(buf_.begin() + FLEXLM_HEADER_SIZE,
                       buf_.begin() + total);
    buf_.erase(buf_.begin(), buf_.begin() + total);
    return pkt;
}

void FrameReader::reset() {
    buf_.clear();
    error_ = false;
}

// ── FrameWriter ───────────────────────────────────────────────────────────────

std::vector<uint8_t> frame_packet(const Packet& pkt) {
    std::vector<uint8_t> wire;
    wire.resize(FLEXLM_HEADER_SIZE + pkt.payload.size());

    uint16_t payload_len = static_cast<uint16_t>(pkt.payload.size());
    uint16_t raw_opcode  = static_cast<uint16_t>(pkt.opcode);

    write_u16_be(wire.data(),     payload_len);
    write_u16_be(wire.data() + 2, raw_opcode);

    if (!pkt.payload.empty())
        std::copy(pkt.payload.begin(), pkt.payload.end(),
                  wire.begin() + FLEXLM_HEADER_SIZE);
    return wire;
}

// ── Blocking I/O helpers ──────────────────────────────────────────────────────

static bool write_all(int fd, const uint8_t* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = ::send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            spdlog::debug("[framing] send failed: {}", strerror(errno));
            return false;
        }
        sent += n;
    }
    return true;
}

static bool read_all(int fd, uint8_t* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(fd, buf + received, len - received, 0);
        if (n <= 0) {
            if (n < 0) spdlog::debug("[framing] recv failed: {}", strerror(errno));
            return false;
        }
        received += n;
    }
    return true;
}

bool send_packet(int fd, const Packet& pkt) {
    auto wire = frame_packet(pkt);
    return write_all(fd, wire.data(), wire.size());
}

std::optional<Packet> recv_packet(int fd) {
    // Read header first
    uint8_t hdr[FLEXLM_HEADER_SIZE];
    if (!read_all(fd, hdr, FLEXLM_HEADER_SIZE)) return std::nullopt;

    uint16_t payload_len = read_u16_be(hdr);
    if (payload_len > FLEXLM_MAX_PACKET) {
        spdlog::error("[framing] recv_packet: oversized payload ({})", payload_len);
        return std::nullopt;
    }

    // Build a fake byte stream and let FrameReader parse it
    FrameReader reader;
    reader.push(hdr, FLEXLM_HEADER_SIZE);

    if (payload_len > 0) {
        std::vector<uint8_t> payload(payload_len);
        if (!read_all(fd, payload.data(), payload_len)) return std::nullopt;
        reader.push(payload.data(), payload_len);
    }

    return reader.pop();
}

} // namespace broker
