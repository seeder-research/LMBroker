#include "api/alerter.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <cstdio>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

namespace api {

// ── helpers ───────────────────────────────────────────────────────────────────

std::string Alerter::now_iso() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

std::string Alerter::make_payload(const Alert& alert) {
    json obj = {
        {"type",      alert_type_name(alert.type)},
        {"subject",   alert.subject},
        {"message",   alert.message},
        {"timestamp", alert.timestamp}
    };
    return obj.dump();
}

bool Alerter::default_send(const std::string& url,
                            const std::string& secret,
                            const std::string& body) {
    // Use curl subprocess — avoids linking libcurl while keeping the feature usable.
    // In production replace with a proper HTTP client.
    std::string cmd = "curl -sf -X POST"
                      " -H 'Content-Type: application/json'";
    if (!secret.empty())
        cmd += " -H 'X-Webhook-Secret: " + secret + "'";
    cmd += " -d '" + body + "'"
           " '" + url + "'"
           " > /dev/null 2>&1";
    int rc = std::system(cmd.c_str());
    return rc == 0;
}

// ── lifecycle ─────────────────────────────────────────────────────────────────

Alerter::Alerter(const common::Config&                 cfg,
                 std::shared_ptr<pool::PoolManager>     pool,
                 std::shared_ptr<tracker::UsageTracker> tracker)
    : cfg_(cfg), pool_(std::move(pool)), tracker_(std::move(tracker)) {}

Alerter::~Alerter() { stop(); }

void Alerter::start() {
    if (cfg_.alerts.webhook_url.empty()) {
        spdlog::info("[alerter] No webhook_url configured — alerting disabled");
        return;
    }
    running_ = true;
    thread_  = std::thread(&Alerter::watch_loop, this);
    spdlog::info("[alerter] Started (webhook={}, cooldown={}s)",
                 cfg_.alerts.webhook_url, cfg_.alerts.cooldown_sec);
}

void Alerter::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

// ── watch loop ────────────────────────────────────────────────────────────────

void Alerter::watch_loop() {
    while (running_) {
        check_backends();
        check_pool_exhaustion();
        check_denial_spike();

        for (int i = 0; i < 300 && running_; ++i)   // 30-second interval
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ── check_backends ────────────────────────────────────────────────────────────

void Alerter::check_backends() {
    for (const auto& bs : pool_->backend_statuses()) {
        std::string key = bs.server.host + ":" + std::to_string(bs.server.port);
        auto it = prev_health_.find(key);
        bool was_healthy = (it == prev_health_.end()) ? true : it->second;

        if (was_healthy && !bs.healthy) {
            maybe_fire(AlertType::SERVER_DOWN, key,
                       "Backend " + key + " is no longer reachable");
        } else if (!was_healthy && bs.healthy) {
            maybe_fire(AlertType::SERVER_UP, key,
                       "Backend " + key + " has recovered");
        }
        prev_health_[key] = bs.healthy;
    }
}

// ── check_pool_exhaustion ─────────────────────────────────────────────────────

void Alerter::check_pool_exhaustion() {
    for (const auto& f : pool_->aggregated_features()) {
        if (f.uncounted || f.total <= 0) continue;
        float pct = static_cast<float>(f.in_use) / static_cast<float>(f.total);
        bool exhausted = (pct >= cfg_.alerts.pool_exhaustion_pct);
        bool was_exhausted = prev_exhausted_.count(f.feature)
                             ? prev_exhausted_[f.feature] : false;

        if (exhausted && !was_exhausted) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.1f%%", pct * 100.0f);
            maybe_fire(AlertType::POOL_EXHAUSTED, f.feature,
                       "Feature " + f.feature + " at " + buf
                       + " utilisation (" + std::to_string(f.in_use)
                       + "/" + std::to_string(f.total) + ")");
        } else if (!exhausted && was_exhausted) {
            maybe_fire(AlertType::POOL_RECOVERED, f.feature,
                       "Feature " + f.feature + " utilisation back below threshold");
        }
        prev_exhausted_[f.feature] = exhausted;
    }
}

// ── check_denial_spike ────────────────────────────────────────────────────────

void Alerter::check_denial_spike() {
    if (!tracker_) return;
    for (const auto& d : tracker_->query_denials_24h()) {
        // Approximate denials/min over 24 h window
        double per_min = static_cast<double>(d.denials_24h) / (24.0 * 60.0);
        if (per_min >= cfg_.alerts.denial_spike_threshold) {
            maybe_fire(AlertType::DENIAL_SPIKE, d.feature,
                       "Feature " + d.feature + " has "
                       + std::to_string(d.denials_24h)
                       + " denials in last 24h ("
                       + std::to_string(static_cast<int>(per_min))
                       + "/min avg)");
        }
    }
}

// ── cooldown logic ────────────────────────────────────────────────────────────

std::string Alerter::cooldown_key(AlertType type,
                                   const std::string& subject) const {
    return std::string(alert_type_name(type)) + ":" + subject;
}

bool Alerter::in_cooldown(const std::string& key) const {
    std::lock_guard<std::mutex> lk(cooldown_mtx_);
    auto it = cooldown_.find(key);
    if (it == cooldown_.end()) return false;
    return std::chrono::steady_clock::now() < it->second;
}

void Alerter::set_cooldown(const std::string& key) {
    std::lock_guard<std::mutex> lk(cooldown_mtx_);
    cooldown_[key] = std::chrono::steady_clock::now()
                   + std::chrono::seconds(cfg_.alerts.cooldown_sec);
}

bool Alerter::maybe_fire(AlertType type, const std::string& subject,
                          const std::string& message) {
    std::string key = cooldown_key(type, subject);
    if (in_cooldown(key)) {
        spdlog::debug("[alerter] Suppressed (cooldown): {} {}", key, message);
        return false;
    }
    Alert alert{type, subject, message, now_iso()};
    fire(alert);
    set_cooldown(key);
    return true;
}

void Alerter::fire(const Alert& alert) {
    std::string payload = make_payload(alert);
    spdlog::warn("[alerter] ALERT type={} subject={} message={}",
                 alert_type_name(alert.type), alert.subject, alert.message);

    if (cfg_.alerts.webhook_url.empty()) return;

    bool ok = sender_(cfg_.alerts.webhook_url,
                      cfg_.alerts.webhook_secret,
                      payload);
    if (!ok)
        spdlog::error("[alerter] Webhook delivery failed to {}",
                      cfg_.alerts.webhook_url);
}

std::vector<std::string> Alerter::suppressed_keys() const {
    std::lock_guard<std::mutex> lk(cooldown_mtx_);
    std::vector<std::string> keys;
    auto now = std::chrono::steady_clock::now();
    for (const auto& [k, exp] : cooldown_)
        if (now < exp) keys.push_back(k);
    return keys;
}

} // namespace api
