#pragma once
#include <vector>
#include <optional>
#include <cstdint>
#include "broker/protocol.h"

namespace broker {

// ── FrameReader ───────────────────────────────────────────────────────────────
// Accumulates raw bytes from a TCP stream and emits complete Packets.
// Callers feed bytes via push(); pop() returns the next complete packet or
// nullopt when more data is needed.
//
// Usage:
//   FrameReader reader;
//   while (true) {
//       ssize_t n = recv(fd, buf, sizeof(buf), 0);
//       if (n <= 0) break;
//       reader.push(buf, n);
//       while (auto pkt = reader.pop()) handle(*pkt);
//   }
class FrameReader {
public:
    void push(const uint8_t* data, size_t len);
    std::optional<Packet> pop();

    bool has_error() const { return error_; }
    void reset();

private:
    std::vector<uint8_t> buf_;
    bool                 error_{false};
};

// ── FrameWriter ───────────────────────────────────────────────────────────────
// Serialises a Packet into a byte buffer ready to send via write()/send().
std::vector<uint8_t> frame_packet(const Packet& pkt);

// Blocking send helper — loops until all bytes are sent or an error occurs.
// Returns true on success.
bool send_packet(int fd, const Packet& pkt);

// Blocking recv helper — reads exactly one complete packet from fd.
// Returns nullopt on EOF or error.
std::optional<Packet> recv_packet(int fd);

} // namespace broker
