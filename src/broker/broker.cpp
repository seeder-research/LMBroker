#include "broker/broker.h"
#include "broker/connection.h"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace broker {

Broker::Broker(const common::Config& cfg,
               std::shared_ptr<pool::PoolManager> pool,
               std::shared_ptr<tracker::UsageTracker> tracker)
    : cfg_(cfg),
      pool_(std::move(pool)),
      tracker_(std::move(tracker)) {}

Broker::~Broker() { stop(); }

void Broker::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        spdlog::error("[broker] socket(): {}", strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(cfg_.broker_port);

    if (::bind(listen_fd_,
               reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[broker] bind(): {}", strerror(errno));
        ::close(listen_fd_); listen_fd_ = -1;
        return;
    }
    ::listen(listen_fd_, 256);

    thread_pool_   = std::make_unique<ThreadPool>(cfg_.broker_threads,
                                                   kDefaultMaxQueue);
    running_       = true;
    accept_thread_ = std::thread(&Broker::accept_loop, this);

    spdlog::info("[broker] Listening on port {} ({} worker threads)",
                 cfg_.broker_port, cfg_.broker_threads);
}

void Broker::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (thread_pool_) thread_pool_->stop();
}

size_t Broker::active_connections() const {
    return active_connections_.load();
}

void Broker::accept_loop() {
    while (running_) {
        sockaddr_in  client_addr{};
        socklen_t    client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_,
                                  reinterpret_cast<sockaddr*>(&client_addr),
                                  &client_len);
        if (client_fd < 0) {
            if (running_) spdlog::debug("[broker] accept(): {}", strerror(errno));
            break;
        }

        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        spdlog::debug("[broker] New connection from {}:{}", ip, client_port);

        Connection::Context ctx;
        ctx.pool        = pool_;
        ctx.tracker     = tracker_;
        ctx.client_ip   = ip;
        ctx.client_port = client_port;

        active_connections_++;
        bool submitted = thread_pool_->submit(
            [this, client_fd, ctx = std::move(ctx)]() mutable {
                Connection conn(client_fd, std::move(ctx));
                conn.run();
                active_connections_--;
            });

        if (!submitted) {
            spdlog::warn("[broker] Thread pool full, dropping connection from {}:{}",
                         ip, client_port);
            ::close(client_fd);
            active_connections_--;
        }
    }
}

} // namespace broker
