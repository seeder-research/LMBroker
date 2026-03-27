#include "broker/connection.h"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <algorithm>

namespace broker {

std::atomic<uint32_t> Connection::next_handle_{1};

// ── Constructor / Destructor ──────────────────────────────────────────────────

Connection::Connection(int client_fd, Context ctx)
    : fd_(client_fd), ctx_(std::move(ctx)) {}

Connection::~Connection() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

// ── run() — main connection loop ──────────────────────────────────────────────

void Connection::run() {
    transition(ConnState::NEGOTIATING);
    spdlog::debug("[conn] {}:{} connected",
                  ctx_.client_ip, ctx_.client_port);

    uint8_t buf[4096];
    while (state_ != ConnState::DONE) {
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n == 0) {
            spdlog::debug("[conn] {}:{} closed connection",
                          ctx_.client_ip, ctx_.client_port);
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            spdlog::debug("[conn] recv error {}: {}",
                          errno, strerror(errno));
            break;
        }

        reader_.push(buf, static_cast<size_t>(n));

        if (reader_.has_error()) {
            spdlog::warn("[conn] Frame error from {}:{} — closing",
                         ctx_.client_ip, ctx_.client_port);
            break;
        }

        while (auto pkt = reader_.pop()) {
            spdlog::debug("[conn] RX opcode={} payload_len={}",
                          opcode_name(pkt->opcode), pkt->payload.size());
            try {
                switch (pkt->opcode) {
                case Opcode::HELLO:     handle_hello(*pkt);     break;
                case Opcode::CHECKOUT:  handle_checkout(*pkt);  break;
                case Opcode::CHECKIN:   handle_checkin(*pkt);   break;
                case Opcode::HEARTBEAT: handle_heartbeat(*pkt); break;
                case Opcode::QUERY:     handle_query(*pkt);     break;
                default:                handle_unknown(*pkt);   break;
                }
            } catch (const std::exception& e) {
                spdlog::warn("[conn] Handler threw: {} — sending ERROR",
                             e.what());
                send_error(1, e.what());
            }
            if (state_ == ConnState::DONE) break;
        }
    }

    // Emit CHECKIN events for any abandoned checkouts
    for (const auto& co : checkouts_) {
        if (ctx_.tracker) {
            tracker::UsageEvent ev;
            ev.type         = tracker::EventType::CHECKIN;
            ev.feature      = co.feature;
            ev.user         = client_username_;
            ev.client_host  = ctx_.client_ip;
            ev.backend_host = co.backend_host;
            ev.backend_port = co.backend_port;
            ctx_.tracker->record(std::move(ev));
        }
    }

    transition(ConnState::DONE);
    spdlog::debug("[conn] {}:{} done", ctx_.client_ip, ctx_.client_port);
}

// ── Packet handlers ───────────────────────────────────────────────────────────

void Connection::handle_hello(const Packet& pkt) {
    if (state_ != ConnState::NEGOTIATING) {
        send_error(2, "Unexpected HELLO");
        return;
    }
    auto msg = HelloMsg::decode(pkt);
    client_username_ = msg.username;
    client_host_     = msg.client_host;

    spdlog::info("[conn] HELLO from user={} host={} ver={}",
                 msg.username, msg.client_host, msg.client_version);

    HelloAckMsg ack;
    ack.server_version = 0x0B130000; // FlexLM 11.19
    ack.server_host    = "flexlm-broker";
    send(ack.encode());

    transition(ConnState::ACTIVE);
}

void Connection::handle_checkout(const Packet& pkt) {
    if (state_ != ConnState::ACTIVE) {
        send_error(3, "Not in ACTIVE state");
        return;
    }
    auto msg = CheckoutMsg::decode(pkt);
    spdlog::info("[conn] CHECKOUT feature={} user={} host={}",
                 msg.feature, msg.username, msg.client_host);

    // Override with negotiated identity if client didn't send it
    if (msg.username.empty())    msg.username    = client_username_;
    if (msg.client_host.empty()) msg.client_host = client_host_;

    const common::ServerEntry* backend =
        ctx_.pool->select_backend(msg.feature);

    if (!backend) {
        // No seats available — send denial
        spdlog::warn("[conn] CHECKOUT denied: no seats for {}",
                     msg.feature);
        if (ctx_.tracker) {
            tracker::UsageEvent ev;
            ev.type          = tracker::EventType::DENIAL;
            ev.feature       = msg.feature;
            ev.user          = msg.username;
            ev.client_host   = msg.client_host;
            ev.denial_reason = "no licenses available";
            ctx_.tracker->record(std::move(ev));
        }
        CheckoutAckMsg ack;
        ack.granted       = false;
        ack.denial_reason = "no licenses available";
        send(ack.encode());
        return;
    }

    // Forward to backend
    int bfd = connect_backend(*backend);
    if (bfd < 0) {
        CheckoutAckMsg ack;
        ack.granted       = false;
        ack.denial_reason = "backend unavailable";
        send(ack.encode());
        return;
    }

    auto resp = proxy_to_backend(bfd, msg.encode());
    ::close(bfd);

    if (!resp) {
        send_error(4, "Backend did not respond to CHECKOUT");
        return;
    }

    // Parse backend response
    auto backend_ack = CheckoutAckMsg::decode(*resp);

    if (backend_ack.granted) {
        // Assign our own handle so we can track it
        uint32_t handle = next_handle_.fetch_add(1);
        backend_ack.handle = handle;

        checkouts_.push_back({msg.feature, handle,
                              backend->host, backend->port});

        if (ctx_.tracker) {
            tracker::UsageEvent ev;
            ev.type         = tracker::EventType::CHECKOUT;
            ev.feature      = msg.feature;
            ev.user         = msg.username;
            ev.client_host  = msg.client_host;
            ev.backend_host = backend->host;
            ev.backend_port = backend->port;
            ctx_.tracker->record(std::move(ev));
        }
        spdlog::info("[conn] CHECKOUT granted feature={} handle={} backend={}:{}",
                     msg.feature, handle, backend->host, backend->port);
    } else {
        if (ctx_.tracker) {
            tracker::UsageEvent ev;
            ev.type          = tracker::EventType::DENIAL;
            ev.feature       = msg.feature;
            ev.user          = msg.username;
            ev.client_host   = msg.client_host;
            ev.backend_host  = backend->host;
            ev.backend_port  = backend->port;
            ev.denial_reason = backend_ack.denial_reason;
            ctx_.tracker->record(std::move(ev));
        }
        spdlog::warn("[conn] CHECKOUT denied by backend: {}",
                     backend_ack.denial_reason);
    }

    send(backend_ack.encode());
}

void Connection::handle_checkin(const Packet& pkt) {
    if (state_ != ConnState::ACTIVE) {
        send_error(3, "Not in ACTIVE state");
        return;
    }
    auto msg = CheckinMsg::decode(pkt);
    spdlog::info("[conn] CHECKIN feature={} handle={}",
                 msg.feature, msg.handle);

    // Find the checkout record matching this handle
    std::string backend_host;
    uint16_t    backend_port = 0;
    auto it = std::find_if(checkouts_.begin(), checkouts_.end(),
        [&](const CheckoutRecord& r) {
            return r.feature == msg.feature && r.handle == msg.handle;
        });
    if (it != checkouts_.end()) {
        backend_host = it->backend_host;
        backend_port = it->backend_port;
        checkouts_.erase(it);
    }

    if (ctx_.tracker) {
        tracker::UsageEvent ev;
        ev.type         = tracker::EventType::CHECKIN;
        ev.feature      = msg.feature;
        ev.user         = client_username_;
        ev.client_host  = ctx_.client_ip;
        ev.backend_host = backend_host;
        ev.backend_port = backend_port;
        ctx_.tracker->record(std::move(ev));
    }

    // Forward checkin to backend if we know which one
    if (!backend_host.empty()) {
        common::ServerEntry srv;
        srv.host = backend_host;
        srv.port = backend_port;
        int bfd  = connect_backend(srv);
        if (bfd >= 0) {
            proxy_to_backend(bfd, msg.encode());
            ::close(bfd);
        }
    }

    CheckinAckMsg ack;
    ack.ok = true;
    send(ack.encode());
}

void Connection::handle_heartbeat(const Packet& pkt) {
    auto msg = HeartbeatMsg::decode(pkt);
    spdlog::debug("[conn] HEARTBEAT seq={}", msg.seq);
    Packet ack;
    ack.opcode = Opcode::HEARTBEAT_ACK;
    ack.payload = pkt.payload; // echo seq back
    send(ack);
}

void Connection::handle_query(const Packet& pkt) {
    auto msg = QueryMsg::decode(pkt);
    spdlog::debug("[conn] QUERY feature='{}'", msg.feature);

    // Build QUERY_ACK from the live pool aggregate
    auto features = ctx_.pool->aggregated_features();
    Packet ack;
    ack.opcode = Opcode::QUERY_ACK;

    // Payload: uint16 feature_count, then for each:
    //   lstring feature, uint32 total, uint32 in_use
    std::vector<pool::FeatureCount> relevant;
    for (const auto& f : features) {
        if (msg.feature.empty() || f.feature == msg.feature)
            relevant.push_back(f);
    }

    ack.payload.resize(2);
    write_u16_be(ack.payload.data(), static_cast<uint16_t>(relevant.size()));
    for (const auto& f : relevant) {
        write_lstring(ack.payload, f.feature);
        size_t pos = ack.payload.size();
        ack.payload.resize(pos + 8);
        write_u32_be(ack.payload.data() + pos,     static_cast<uint32_t>(f.total));
        write_u32_be(ack.payload.data() + pos + 4, static_cast<uint32_t>(f.in_use));
    }
    send(ack);
}

void Connection::handle_unknown(const Packet& pkt) {
    spdlog::debug("[conn] Unknown opcode 0x{:04X} ({}B payload) — proxying transparently",
                  static_cast<uint16_t>(pkt.opcode), pkt.payload.size());
    // For unknown opcodes we pass through to the first healthy backend
    auto statuses = ctx_.pool->backend_statuses();
    for (const auto& bs : statuses) {
        if (!bs.healthy) continue;
        int bfd = connect_backend(bs.server);
        if (bfd < 0) continue;
        if (auto resp = proxy_to_backend(bfd, pkt)) {
            send(*resp);
        }
        ::close(bfd);
        return;
    }
    send_error(5, "No backend available for unknown opcode");
}

// ── Backend connection helpers ────────────────────────────────────────────────

int Connection::connect_backend(const common::ServerEntry& srv) {
    int bfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (bfd < 0) return -1;

    // 5-second connect timeout via SO_SNDTIMEO
    struct timeval tv{5, 0};
    ::setsockopt(bfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(bfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(srv.port);
    if (::inet_pton(AF_INET, srv.host.c_str(), &addr.sin_addr) <= 0) {
        // Try hostname resolution via getaddrinfo (simple synchronous)
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        std::string port_str = std::to_string(srv.port);
        if (::getaddrinfo(srv.host.c_str(), port_str.c_str(), &hints, &res) != 0) {
            ::close(bfd);
            return -1;
        }
        addr = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        ::freeaddrinfo(res);
    }

    if (::connect(bfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::debug("[conn] connect to {}:{} failed: {}",
                      srv.host, srv.port, strerror(errno));
        ::close(bfd);
        return -1;
    }
    return bfd;
}

std::optional<Packet> Connection::proxy_to_backend(int bfd, const Packet& pkt) {
    if (!send_packet(bfd, pkt)) return std::nullopt;
    return recv_packet(bfd);
}

// ── Utilities ─────────────────────────────────────────────────────────────────

void Connection::send(const Packet& pkt) {
    if (!send_packet(fd_, pkt)) {
        spdlog::debug("[conn] send failed to {}:{}", ctx_.client_ip, ctx_.client_port);
        transition(ConnState::CLOSING);
    }
}

void Connection::send_error(uint32_t code, const std::string& msg) {
    ErrorMsg err;
    err.code    = code;
    err.message = msg;
    send(err.encode());
}

void Connection::transition(ConnState next) {
    spdlog::debug("[conn] {}:{} {} → {}",
                  ctx_.client_ip, ctx_.client_port,
                  state_name(state_), state_name(next));
    state_ = next;
}

} // namespace broker
