#pragma once
#include <string>
#include <memory>
#include <atomic>
#include <cstdint>
#include "broker/protocol.h"
#include "broker/framing.h"
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

namespace broker {

// ── Connection state machine ──────────────────────────────────────────────────
//
//   INIT ──► NEGOTIATING ──► ACTIVE ──► CLOSING ──► DONE
//              │                           ▲
//              └──────── error ────────────┘

enum class ConnState {
    INIT,
    NEGOTIATING,  // awaiting HELLO / HELLO_ACK exchange
    ACTIVE,       // normal operation
    CLOSING,
    DONE
};

inline const char* state_name(ConnState s) {
    switch (s) {
    case ConnState::INIT:        return "INIT";
    case ConnState::NEGOTIATING: return "NEGOTIATING";
    case ConnState::ACTIVE:      return "ACTIVE";
    case ConnState::CLOSING:     return "CLOSING";
    case ConnState::DONE:        return "DONE";
    }
    return "?";
}

// ── Connection ────────────────────────────────────────────────────────────────
// Handles one client TCP connection end-to-end.
// Constructed on the worker thread; run() blocks until the connection closes.

class Connection {
public:
    struct Context {
        std::shared_ptr<pool::PoolManager>      pool;
        std::shared_ptr<tracker::UsageTracker>  tracker;
        std::string                             client_ip;
        uint16_t                                client_port{0};
    };

    Connection(int client_fd, Context ctx);
    ~Connection();

    // Runs the connection loop on the calling thread.
    // Returns when the connection is fully closed.
    void run();

    ConnState state() const { return state_; }

private:
    // ── Packet handlers ───────────────────────────────────────────────────
    void handle_hello(const Packet& pkt);
    void handle_checkout(const Packet& pkt);
    void handle_checkin(const Packet& pkt);
    void handle_heartbeat(const Packet& pkt);
    void handle_query(const Packet& pkt);
    void handle_unknown(const Packet& pkt);

    // ── Backend proxy helpers ─────────────────────────────────────────────
    // Open a fresh TCP connection to the given backend server entry.
    // Returns fd >= 0 on success, -1 on failure.
    int  connect_backend(const common::ServerEntry& srv);

    // Forward pkt to backend fd; read one response packet and return it.
    std::optional<Packet> proxy_to_backend(int backend_fd, const Packet& pkt);

    // ── Utilities ─────────────────────────────────────────────────────────
    void send(const Packet& pkt);
    void send_error(uint32_t code, const std::string& msg);
    void transition(ConnState next);

    // ── State ─────────────────────────────────────────────────────────────
    int         fd_;           // client socket fd
    Context     ctx_;
    ConnState   state_{ConnState::INIT};
    FrameReader reader_;

    // Negotiated client info (populated during HELLO exchange)
    std::string client_username_;
    std::string client_host_;

    // Track outstanding checkouts: feature → handle
    // Used to match CHECKIN events to the correct tracker record
    struct CheckoutRecord {
        std::string feature;
        uint32_t    handle{0};
        std::string backend_host;
        uint16_t    backend_port{0};
    };
    std::vector<CheckoutRecord> checkouts_;

    static std::atomic<uint32_t> next_handle_;
};

} // namespace broker
