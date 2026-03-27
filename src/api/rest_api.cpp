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
                 std::shared_ptr<tracker::UsageTracker> tracker)
    : cfg_(cfg),
      pool_(std::move(pool)),
      tracker_(std::move(tracker)),
      impl_(std::make_unique<Impl>()) {}

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
    svr.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
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
