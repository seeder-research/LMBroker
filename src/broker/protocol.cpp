#include "broker/protocol.h"
#include <stdexcept>

namespace broker {

// ── HelloMsg ──────────────────────────────────────────────────────────────────

HelloMsg HelloMsg::decode(const Packet& pkt) {
    const auto& p = pkt.payload;
    if (p.size() < 4) throw std::runtime_error("HELLO payload too short");
    HelloMsg m;
    m.client_version = read_u32_be(p.data());
    size_t off = 4;
    m.client_host = read_lstring(p.data(), p.size(), off);
    m.username    = read_lstring(p.data(), p.size(), off);
    return m;
}

Packet HelloMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::HELLO;
    write_u32_be(reinterpret_cast<uint8_t*>(pkt.payload.data()), 0);
    pkt.payload.resize(4);
    write_u32_be(pkt.payload.data(), client_version);
    write_lstring(pkt.payload, client_host);
    write_lstring(pkt.payload, username);
    return pkt;
}

// ── HelloAckMsg ───────────────────────────────────────────────────────────────

HelloAckMsg HelloAckMsg::decode(const Packet& pkt) {
    const auto& p = pkt.payload;
    if (p.size() < 4) throw std::runtime_error("HELLO_ACK payload too short");
    HelloAckMsg m;
    m.server_version = read_u32_be(p.data());
    size_t off = 4;
    m.server_host = read_lstring(p.data(), p.size(), off);
    return m;
}

Packet HelloAckMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::HELLO_ACK;
    pkt.payload.resize(4);
    write_u32_be(pkt.payload.data(), server_version);
    write_lstring(pkt.payload, server_host);
    return pkt;
}

// ── CheckoutMsg ───────────────────────────────────────────────────────────────

CheckoutMsg CheckoutMsg::decode(const Packet& pkt) {
    const auto& p = pkt.payload;
    CheckoutMsg m;
    size_t off = 0;
    m.feature     = read_lstring(p.data(), p.size(), off);
    m.version     = read_lstring(p.data(), p.size(), off);
    m.username    = read_lstring(p.data(), p.size(), off);
    m.client_host = read_lstring(p.data(), p.size(), off);
    m.display     = read_lstring(p.data(), p.size(), off);
    if (off + 4 <= p.size())
        m.count = read_u32_be(p.data() + off);
    return m;
}

Packet CheckoutMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::CHECKOUT;
    write_lstring(pkt.payload, feature);
    write_lstring(pkt.payload, version);
    write_lstring(pkt.payload, username);
    write_lstring(pkt.payload, client_host);
    write_lstring(pkt.payload, display);
    pkt.payload.resize(pkt.payload.size() + 4);
    write_u32_be(pkt.payload.data() + pkt.payload.size() - 4, count);
    return pkt;
}

// ── CheckoutAckMsg ────────────────────────────────────────────────────────────

CheckoutAckMsg CheckoutAckMsg::decode(const Packet& pkt) {
    const auto& p = pkt.payload;
    CheckoutAckMsg m;
    if (p.size() < 1) throw std::runtime_error("CHECKOUT_ACK too short");
    m.granted = (p[0] != 0);
    size_t off = 1;
    if (m.granted && off + 4 <= p.size()) {
        m.handle = read_u32_be(p.data() + off);
    } else if (!m.granted) {
        m.denial_reason = read_lstring(p.data(), p.size(), off);
    }
    return m;
}

Packet CheckoutAckMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::CHECKOUT_ACK;
    pkt.payload.push_back(granted ? 1 : 0);
    if (granted) {
        pkt.payload.resize(pkt.payload.size() + 4);
        write_u32_be(pkt.payload.data() + pkt.payload.size() - 4, handle);
    } else {
        write_lstring(pkt.payload, denial_reason);
    }
    return pkt;
}

// ── CheckinMsg ────────────────────────────────────────────────────────────────

CheckinMsg CheckinMsg::decode(const Packet& pkt) {
    const auto& p = pkt.payload;
    CheckinMsg m;
    size_t off = 0;
    m.feature = read_lstring(p.data(), p.size(), off);
    if (off + 4 <= p.size()) m.handle = read_u32_be(p.data() + off);
    return m;
}

Packet CheckinMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::CHECKIN;
    write_lstring(pkt.payload, feature);
    pkt.payload.resize(pkt.payload.size() + 4);
    write_u32_be(pkt.payload.data() + pkt.payload.size() - 4, handle);
    return pkt;
}

// ── CheckinAckMsg ─────────────────────────────────────────────────────────────

CheckinAckMsg CheckinAckMsg::decode(const Packet& pkt) {
    CheckinAckMsg m;
    m.ok = pkt.payload.empty() || pkt.payload[0] != 0;
    return m;
}

Packet CheckinAckMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::CHECKIN_ACK;
    pkt.payload.push_back(ok ? 1 : 0);
    return pkt;
}

// ── HeartbeatMsg ──────────────────────────────────────────────────────────────

HeartbeatMsg HeartbeatMsg::decode(const Packet& pkt) {
    HeartbeatMsg m;
    if (pkt.payload.size() >= 4) m.seq = read_u32_be(pkt.payload.data());
    return m;
}

Packet HeartbeatMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::HEARTBEAT;
    pkt.payload.resize(4);
    write_u32_be(pkt.payload.data(), seq);
    return pkt;
}

// ── QueryMsg ──────────────────────────────────────────────────────────────────

QueryMsg QueryMsg::decode(const Packet& pkt) {
    QueryMsg m;
    if (!pkt.payload.empty()) {
        size_t off = 0;
        m.feature = read_lstring(pkt.payload.data(), pkt.payload.size(), off);
    }
    return m;
}

Packet QueryMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::QUERY;
    if (!feature.empty()) write_lstring(pkt.payload, feature);
    return pkt;
}

// ── ErrorMsg ──────────────────────────────────────────────────────────────────

ErrorMsg ErrorMsg::decode(const Packet& pkt) {
    ErrorMsg m;
    const auto& p = pkt.payload;
    if (p.size() >= 4) m.code = read_u32_be(p.data());
    if (p.size() > 4) {
        size_t off = 4;
        m.message = read_lstring(p.data(), p.size(), off);
    }
    return m;
}

Packet ErrorMsg::encode() const {
    Packet pkt;
    pkt.opcode = Opcode::ERROR;
    pkt.payload.resize(4);
    write_u32_be(pkt.payload.data(), code);
    write_lstring(pkt.payload, message);
    return pkt;
}

} // namespace broker
