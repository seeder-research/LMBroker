#include <gtest/gtest.h>
#include "broker/protocol.h"

using namespace broker;

// ── HelloMsg ──────────────────────────────────────────────────────────────────

TEST(Protocol, HelloRoundTrip) {
    HelloMsg orig;
    orig.client_version = 0x000B1300;
    orig.client_host    = "workstation1";
    orig.username       = "jsmith";

    auto pkt  = orig.encode();
    EXPECT_EQ(pkt.opcode, Opcode::HELLO);

    auto decoded = HelloMsg::decode(pkt);
    EXPECT_EQ(decoded.client_version, orig.client_version);
    EXPECT_EQ(decoded.client_host,    orig.client_host);
    EXPECT_EQ(decoded.username,       orig.username);
}

// ── HelloAckMsg ───────────────────────────────────────────────────────────────

TEST(Protocol, HelloAckRoundTrip) {
    HelloAckMsg orig;
    orig.server_version = 0x0B130000;
    orig.server_host    = "flexlm-broker";

    auto decoded = HelloAckMsg::decode(orig.encode());
    EXPECT_EQ(decoded.server_version, orig.server_version);
    EXPECT_EQ(decoded.server_host,    orig.server_host);
}

// ── CheckoutMsg ───────────────────────────────────────────────────────────────

TEST(Protocol, CheckoutRoundTrip) {
    CheckoutMsg orig;
    orig.feature     = "MATLAB";
    orig.version     = "24.0";
    orig.username    = "user1";
    orig.client_host = "ws1";
    orig.display     = ":0";
    orig.count       = 2;

    auto decoded = CheckoutMsg::decode(orig.encode());
    EXPECT_EQ(decoded.feature,     orig.feature);
    EXPECT_EQ(decoded.version,     orig.version);
    EXPECT_EQ(decoded.username,    orig.username);
    EXPECT_EQ(decoded.client_host, orig.client_host);
    EXPECT_EQ(decoded.display,     orig.display);
    EXPECT_EQ(decoded.count,       orig.count);
}

// ── CheckoutAckMsg ────────────────────────────────────────────────────────────

TEST(Protocol, CheckoutAckGranted) {
    CheckoutAckMsg orig;
    orig.granted = true;
    orig.handle  = 42;

    auto decoded = CheckoutAckMsg::decode(orig.encode());
    EXPECT_TRUE(decoded.granted);
    EXPECT_EQ(decoded.handle, 42u);
}

TEST(Protocol, CheckoutAckDenied) {
    CheckoutAckMsg orig;
    orig.granted       = false;
    orig.denial_reason = "no licenses available";

    auto decoded = CheckoutAckMsg::decode(orig.encode());
    EXPECT_FALSE(decoded.granted);
    EXPECT_EQ(decoded.denial_reason, orig.denial_reason);
}

// ── CheckinMsg ────────────────────────────────────────────────────────────────

TEST(Protocol, CheckinRoundTrip) {
    CheckinMsg orig;
    orig.feature = "MATLAB";
    orig.handle  = 99;

    auto decoded = CheckinMsg::decode(orig.encode());
    EXPECT_EQ(decoded.feature, orig.feature);
    EXPECT_EQ(decoded.handle,  orig.handle);
}

// ── HeartbeatMsg ──────────────────────────────────────────────────────────────

TEST(Protocol, HeartbeatRoundTrip) {
    HeartbeatMsg orig;
    orig.seq = 1234;

    auto decoded = HeartbeatMsg::decode(orig.encode());
    EXPECT_EQ(decoded.seq, orig.seq);
}

// ── QueryMsg ──────────────────────────────────────────────────────────────────

TEST(Protocol, QueryWithFeature) {
    QueryMsg orig;
    orig.feature = "Simulink";

    auto decoded = QueryMsg::decode(orig.encode());
    EXPECT_EQ(decoded.feature, orig.feature);
}

TEST(Protocol, QueryAllFeatures) {
    QueryMsg orig; // empty feature = query all
    auto decoded = QueryMsg::decode(orig.encode());
    EXPECT_TRUE(decoded.feature.empty());
}

// ── ErrorMsg ──────────────────────────────────────────────────────────────────

TEST(Protocol, ErrorRoundTrip) {
    ErrorMsg orig;
    orig.code    = 96;
    orig.message = "license server not started";

    auto decoded = ErrorMsg::decode(orig.encode());
    EXPECT_EQ(decoded.code,    orig.code);
    EXPECT_EQ(decoded.message, orig.message);
}

// ── opcode_name ───────────────────────────────────────────────────────────────

TEST(Protocol, OpcodeNames) {
    EXPECT_STREQ(opcode_name(Opcode::HELLO),    "HELLO");
    EXPECT_STREQ(opcode_name(Opcode::CHECKOUT), "CHECKOUT");
    EXPECT_STREQ(opcode_name(Opcode::UNKNOWN),  "UNKNOWN");
}
