#include <gtest/gtest.h>
#include "broker/framing.h"
#include "broker/protocol.h"

using namespace broker;

// ── frame_packet / FrameReader round-trip ─────────────────────────────────────

TEST(Framing, RoundTripEmptyPayload) {
    Packet pkt;
    pkt.opcode = Opcode::HEARTBEAT;
    auto wire = frame_packet(pkt);
    ASSERT_EQ(wire.size(), FLEXLM_HEADER_SIZE);

    FrameReader reader;
    reader.push(wire.data(), wire.size());
    auto out = reader.pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->opcode, Opcode::HEARTBEAT);
    EXPECT_TRUE(out->payload.empty());
}

TEST(Framing, RoundTripWithPayload) {
    Packet pkt;
    pkt.opcode  = Opcode::CHECKOUT;
    pkt.payload = {0x01, 0x02, 0x03, 0x04};
    auto wire = frame_packet(pkt);
    ASSERT_EQ(wire.size(), FLEXLM_HEADER_SIZE + 4);

    FrameReader reader;
    reader.push(wire.data(), wire.size());
    auto out = reader.pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->opcode, Opcode::CHECKOUT);
    EXPECT_EQ(out->payload, pkt.payload);
}

TEST(Framing, PartialReceive) {
    Packet pkt;
    pkt.opcode  = Opcode::QUERY;
    pkt.payload = {0xAA, 0xBB};
    auto wire = frame_packet(pkt);

    FrameReader reader;
    // Feed one byte at a time
    for (size_t i = 0; i < wire.size() - 1; ++i) {
        reader.push(wire.data() + i, 1);
        EXPECT_FALSE(reader.pop().has_value()) << "Should not have packet after byte " << i;
    }
    reader.push(wire.data() + wire.size() - 1, 1);
    auto out = reader.pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->opcode, Opcode::QUERY);
}

TEST(Framing, MultiplePacketsInOneBuffer) {
    Packet p1, p2;
    p1.opcode  = Opcode::HELLO;
    p1.payload = {0x01};
    p2.opcode  = Opcode::HEARTBEAT;
    p2.payload = {0x02, 0x03};

    auto w1 = frame_packet(p1);
    auto w2 = frame_packet(p2);
    w1.insert(w1.end(), w2.begin(), w2.end());

    FrameReader reader;
    reader.push(w1.data(), w1.size());

    auto out1 = reader.pop();
    ASSERT_TRUE(out1.has_value());
    EXPECT_EQ(out1->opcode, Opcode::HELLO);

    auto out2 = reader.pop();
    ASSERT_TRUE(out2.has_value());
    EXPECT_EQ(out2->opcode, Opcode::HEARTBEAT);

    EXPECT_FALSE(reader.pop().has_value());
}

TEST(Framing, OversizedPacketSetsError) {
    // Manually craft a header claiming an oversized payload
    uint8_t wire[4];
    write_u16_be(wire,     FLEXLM_MAX_PACKET + 1); // payload_len
    write_u16_be(wire + 2, 0x0001);                // opcode HELLO

    FrameReader reader;
    reader.push(wire, 4);
    reader.pop(); // trigger check
    EXPECT_TRUE(reader.has_error());
}

TEST(Framing, UnknownOpcodePreserved) {
    uint8_t wire[4];
    write_u16_be(wire,     0);      // no payload
    write_u16_be(wire + 2, 0xBEEF); // unknown opcode

    FrameReader reader;
    reader.push(wire, 4);
    auto out = reader.pop();
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->opcode, Opcode::UNKNOWN);
}

// ── byte-order helpers ────────────────────────────────────────────────────────

TEST(ByteOrder, U16RoundTrip) {
    uint8_t buf[2];
    write_u16_be(buf, 0xABCD);
    EXPECT_EQ(read_u16_be(buf), 0xABCD);
}

TEST(ByteOrder, U32RoundTrip) {
    uint8_t buf[4];
    write_u32_be(buf, 0xDEADBEEF);
    EXPECT_EQ(read_u32_be(buf), 0xDEADBEEF);
}

// ── lstring codec ─────────────────────────────────────────────────────────────

TEST(LString, WriteAndRead) {
    std::vector<uint8_t> buf;
    write_lstring(buf, "MATLAB");
    ASSERT_EQ(buf.size(), 7u); // 1 len byte + 6 chars

    size_t off = 0;
    auto s = read_lstring(buf.data(), buf.size(), off);
    EXPECT_EQ(s, "MATLAB");
    EXPECT_EQ(off, 7u);
}

TEST(LString, EmptyString) {
    std::vector<uint8_t> buf;
    write_lstring(buf, "");
    ASSERT_EQ(buf.size(), 1u);
    size_t off = 0;
    auto s = read_lstring(buf.data(), buf.size(), off);
    EXPECT_TRUE(s.empty());
}

TEST(LString, TooLongThrows) {
    std::string s(256, 'x');
    std::vector<uint8_t> buf;
    EXPECT_THROW(write_lstring(buf, s), std::invalid_argument);
}
