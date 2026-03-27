#include "broker/broker.h"
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
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
        spdlog::error("[broker] socket() failed: {}", std::strerror(errno));
        return;
    }

    int opt = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(cfg_.broker_port);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[broker] bind() failed: {}", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }
    ::listen(listen_fd_, 128);

    running_       = true;
    accept_thread_ = std::thread(&Broker::accept_loop, this);
    spdlog::info("[broker] Listening on port {}", cfg_.broker_port);
}

void Broker::stop() {
    running_ = false;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

void Broker::accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd_,
                                  reinterpret_cast<sockaddr*>(&client_addr),
                                  &client_len);
        if (client_fd < 0) {
            if (running_) spdlog::warn("[broker] accept() error: {}", std::strerror(errno));
            break;
        }
        char ip[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        spdlog::debug("[broker] Client connected: {}:{}", ip, ntohs(client_addr.sin_port));

        // Detach a worker thread per connection.
        // TODO Phase 4: replace with thread pool.
        std::thread([this, client_fd]{ handle_client(client_fd); }).detach();
    }
}

void Broker::handle_client(int fd) {
    // TODO Phase 4: implement FlexLM protocol framing.
    // For now: read whatever the client sends, log it, close.
    char buf[1024];
    ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        spdlog::debug("[broker] Received {} bytes from client fd={}", n, fd);
    }
    ::close(fd);
}

} // namespace broker
