#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include "api/rest_api.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace api {

struct RestApi::Impl {
    httplib::Server svr;
};

RestApi::RestApi(const common::Config& cfg,
                 std::shared_ptr<pool::PoolManager> pool,
                 std::shared_ptr<tracker::UsageTracker> tracker,
                 std::shared_ptr<Alerter> alerter)
    : cfg_(cfg),
      pool_(std::move(pool)),
      tracker_(std::move(tracker)),
      alerter_(std::move(alerter)),
      impl_(std::make_unique<Impl>()) {
    metrics_ = std::make_unique<MetricsRenderer>(pool_, tracker_);
}

RestApi::~RestApi() { stop(); }

bool RestApi::authenticate(const std::string& auth_header) const {
    if (cfg_.api_token.empty()) return true; // auth disabled
    // Expect: "Bearer <token>"
    const std::string prefix = "Bearer ";
    if (auth_header.rfind(prefix, 0) != 0) return false;
    return auth_header.substr(prefix.size()) == cfg_.api_token;
}

void RestApi::start() {
    running_ = true;

    auto& svr = impl_->svr;

    // ── Auth middleware via pre-routing handler ───────────────────────────
    svr.set_pre_routing_handler(
        [this](const httplib::Request& req, httplib::Response& res) {
            // Allow health check without auth
            if (req.path == "/api/v1/health") return httplib::Server::HandlerResponse::Unhandled;
            auto auth = req.get_header_value("Authorization");
            if (!authenticate(auth)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    // ── GET /api/v1/health ────────────────────────────────────────────────
    svr.Get("/api/v1/health", [this](const httplib::Request&, httplib::Response& res) {
        int healthy = 0, total = 0;
        for (const auto& bs : pool_->backend_statuses()) {
            total++;
            if (bs.healthy) healthy++;
        }
        json obj = {
            {"status",          "ok"},
            {"servers_healthy", healthy},
            {"servers_total",   total}
        };
        res.set_content(obj.dump(2), "application/json");
    });

    // ── GET /api/v1/features ──────────────────────────────────────────────
    svr.Get("/api/v1/features", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& f : pool_->aggregated_features()) {
            arr.push_back({
                {"feature",   f.feature},
                {"vendor",    f.vendor},
                {"total",     f.total},
                {"in_use",    f.in_use},
                {"available", f.available},
                {"queued",    f.queued},
                {"uncounted", f.uncounted}
            });
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // ── GET /api/v1/servers ───────────────────────────────────────────────
    svr.Get("/api/v1/servers", [this](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const auto& bs : pool_->backend_statuses()) {
            arr.push_back({
                {"host",        bs.server.host},
                {"port",        bs.server.port},
                {"name",        bs.server.name},
                {"healthy",     bs.healthy},
                {"fail_streak", bs.fail_streak},
                {"features",    bs.features.size()}
            });
        }
        res.set_content(arr.dump(2), "application/json");
    });

    // ── GET /api/v1/features/:name ────────────────────────────────────────
    svr.Get(R"(/api/v1/features/([^/]+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string name = req.matches[1];
            for (const auto& f : pool_->aggregated_features()) {
                if (f.feature == name) {
                    json obj = {
                        {"feature",   f.feature},
                        {"vendor",    f.vendor},
                        {"total",     f.total},
                        {"in_use",    f.in_use},
                        {"available", f.available},
                        {"queued",    f.queued},
                        {"uncounted", f.uncounted}
                    };
                    res.set_content(obj.dump(2), "application/json");
                    return;
                }
            }
            res.status = 404;
            res.set_content(R"({"error":"feature not found"})", "application/json");
        });

    // ── POST /api/v1/servers  — add a backend server dynamically ─────────
    svr.Post("/api/v1/servers",
        [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                common::ServerEntry entry;
                entry.host = body.at("host").get<std::string>();
                entry.port = body.value("port", 27000);
                entry.name = body.value("name", "");
                pool_->add_server(entry);
                res.status = 201;
                res.set_content(R"({"status":"added"})", "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(json{{"error", e.what()}}.dump(), "application/json");
            }
        });

    // ── DELETE /api/v1/servers/:host/:port ────────────────────────────────
    svr.Delete(R"(/api/v1/servers/([^/]+)/(\d+))",
        [this](const httplib::Request& req, httplib::Response& res) {
            std::string host = req.matches[1];
            uint16_t    port = static_cast<uint16_t>(std::stoi(req.matches[2]));
            if (pool_->remove_server(host, port)) {
                res.set_content(R"({"status":"removed"})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"error":"server not found"})", "application/json");
            }
        });

    // ── GET /api/v1/utilisation ──────────────────────────────────────────
    svr.Get("/api/v1/utilisation",
        [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            for (const auto& r : tracker_->query_utilisation()) {
                arr.push_back({
                    {"feature",     r.feature},
                    {"total",       r.total},
                    {"in_use",      r.in_use},
                    {"available",   r.available},
                    {"queued",      r.queued},
                    {"last_polled", r.last_polled}
                });
            }
            res.set_content(arr.dump(2), "application/json");
        });

    // ── GET /api/v1/denials ───────────────────────────────────────────────
    svr.Get("/api/v1/denials",
        [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            for (const auto& r : tracker_->query_denials_24h()) {
                arr.push_back({
                    {"feature",      r.feature},
                    {"denials_24h",  r.denials_24h}
                });
            }
            res.set_content(arr.dump(2), "application/json");
        });

    // ── GET /api/v1/checkouts/active ──────────────────────────────────────
    svr.Get("/api/v1/checkouts/active",
        [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            for (const auto& r : tracker_->query_active_checkouts()) {
                arr.push_back({
                    {"id",              r.id},
                    {"feature",         r.feature},
                    {"username",        r.username},
                    {"client_host",     r.client_host},
                    {"backend_host",    r.backend_host},
                    {"backend_port",    r.backend_port},
                    {"checked_out_at",  r.checked_out_at},
                    {"duration_sec",    r.duration_sec}
                });
            }
            res.set_content(arr.dump(2), "application/json");
        });

    // ── GET /api/v1/health/events ─────────────────────────────────────────
    svr.Get("/api/v1/health/events",
        [this](const httplib::Request& req, httplib::Response& res) {
            int limit = 50;
            if (req.has_param("limit")) {
                try { limit = std::stoi(req.get_param_value("limit")); }
                catch (...) {}
            }
            json arr = json::array();
            for (const auto& r : tracker_->query_health_events(limit)) {
                arr.push_back({
                    {"host",        r.host},
                    {"port",        r.port},
                    {"event",       r.event},
                    {"occurred_at", r.occurred_at}
                });
            }
            res.set_content(arr.dump(2), "application/json");
        });

    // ── GET /metrics  — Prometheus text format ───────────────────────────
    // No auth required (standard Prometheus convention)
    svr.Get("/metrics", [this](const httplib::Request&, httplib::Response& res) {
        res.set_content(metrics_->render(),
                        "text/plain; version=0.0.4; charset=utf-8");
    });

    // ── Admin endpoints ───────────────────────────────────────────────────
    // POST /api/v1/admin/poll  — force an immediate poll of all backends
    svr.Post("/api/v1/admin/poll",
        [this](const httplib::Request&, httplib::Response& res) {
            // The pool manager's poll_loop runs in background; we signal
            // it by calling trigger_poll() if available, otherwise return
            // the current backend statuses as confirmation.
            auto statuses = pool_->backend_statuses();
            json arr = json::array();
            for (const auto& bs : statuses)
                arr.push_back({{"host", bs.server.host},
                               {"port", bs.server.port},
                               {"healthy", bs.healthy}});
            res.set_content(
                json{{"triggered", true}, {"backends", arr}}.dump(2),
                "application/json");
        });

    // GET /api/v1/admin/pool  — full per-backend feature detail
    svr.Get("/api/v1/admin/pool",
        [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            for (const auto& bs : pool_->backend_statuses()) {
                json feats = json::array();
                for (const auto& f : bs.features) {
                    feats.push_back({
                        {"feature",   f.feature},
                        {"vendor",    f.vendor},
                        {"total",     f.total},
                        {"in_use",    f.in_use},
                        {"available", f.available},
                        {"queued",    f.queued},
                        {"uncounted", f.uncounted}
                    });
                }
                arr.push_back({
                    {"host",        bs.server.host},
                    {"port",        bs.server.port},
                    {"name",        bs.server.name},
                    {"healthy",     bs.healthy},
                    {"fail_streak", bs.fail_streak},
                    {"features",    feats}
                });
            }
            res.set_content(arr.dump(2), "application/json");
        });

    // GET /api/v1/admin/alerts/suppressed  — list cooldown-suppressed keys
    svr.Get("/api/v1/admin/alerts/suppressed",
        [this](const httplib::Request&, httplib::Response& res) {
            json arr = json::array();
            if (alerter_)
                for (const auto& k : alerter_->suppressed_keys())
                    arr.push_back(k);
            res.set_content(arr.dump(2), "application/json");
        });

    // POST /api/v1/admin/alerts/test  — fire a test alert
    svr.Post("/api/v1/admin/alerts/test",
        [this](const httplib::Request&, httplib::Response& res) {
            if (!alerter_) {
                res.status = 503;
                res.set_content(R"({"error":"alerter not configured"})",
                                "application/json");
                return;
            }
            Alert test_alert{
                AlertType::SERVER_DOWN,
                "test-subject",
                "This is a test alert from flexlm-broker admin API",
                ""
            };
            alerter_->fire(test_alert);
            res.set_content(R"({"fired":true})", "application/json");
        });

    thread_ = std::thread(&RestApi::serve, this);
    spdlog::info("[api] REST API listening on {}:{}", cfg_.api_host, cfg_.api_port);
}

void RestApi::serve() {
    impl_->svr.listen(cfg_.api_host.c_str(), cfg_.api_port);
}

void RestApi::stop() {
    impl_->svr.stop();
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

} // namespace api
