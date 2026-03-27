#include "api/metrics.h"
#include <sstream>
#include <algorithm>

namespace api {

MetricsRenderer::MetricsRenderer(
        std::shared_ptr<pool::PoolManager>     pool,
        std::shared_ptr<tracker::UsageTracker> tracker)
    : pool_(std::move(pool)), tracker_(std::move(tracker)) {}

// ── label escaping ────────────────────────────────────────────────────────────
// Prometheus label values must have \ and " escaped, and no newlines.
std::string MetricsRenderer::escape_label(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') { out += "\\\\"; }
        else if (c == '"')  { out += "\\\""; }
        else if (c == '\n') { out += "\\n"; }
        else { out += c; }
    }
    return out;
}

void MetricsRenderer::write_header(std::ostringstream& out,
                                    const std::string&  name,
                                    const std::string&  help,
                                    const std::string&  type) {
    out << "# HELP " << name << " " << help << "\n";
    out << "# TYPE " << name << " " << type << "\n";
}

void MetricsRenderer::write_gauge(std::ostringstream& out,
                                   const std::string&  name,
                                   const std::string&  help,
                                   const std::string&  labels,
                                   double              value) {
    (void)help; // help written once via write_header
    if (labels.empty())
        out << name << " " << value << "\n";
    else
        out << name << "{" << labels << "} " << value << "\n";
}

// ── render() ──────────────────────────────────────────────────────────────────

std::string MetricsRenderer::render() const {
    std::ostringstream out;

    // ── broker build info ─────────────────────────────────────────────────
    write_header(out, "flexlm_broker_info",
                 "FlexLM broker build information");
    out << "flexlm_broker_info{version=\"0.1.0\"} 1\n\n";

    // ── per-feature gauges (from live pool aggregate) ─────────────────────
    auto features = pool_->aggregated_features();

    write_header(out, "flexlm_feature_total",
                 "Total licensed seats per feature");
    for (const auto& f : features) {
        if (f.uncounted) continue;
        std::string lbl = "feature=\"" + escape_label(f.feature) + "\","
                          "vendor=\""  + escape_label(f.vendor)  + "\"";
        out << "flexlm_feature_total{" << lbl << "} " << f.total << "\n";
    }
    out << "\n";

    write_header(out, "flexlm_feature_in_use",
                 "Seats currently in use per feature");
    for (const auto& f : features) {
        if (f.uncounted) continue;
        std::string lbl = "feature=\"" + escape_label(f.feature) + "\","
                          "vendor=\""  + escape_label(f.vendor)  + "\"";
        out << "flexlm_feature_in_use{" << lbl << "} " << f.in_use << "\n";
    }
    out << "\n";

    write_header(out, "flexlm_feature_available",
                 "Seats currently available per feature");
    for (const auto& f : features) {
        if (f.uncounted) continue;
        std::string lbl = "feature=\"" + escape_label(f.feature) + "\","
                          "vendor=\""  + escape_label(f.vendor)  + "\"";
        out << "flexlm_feature_available{" << lbl << "} " << f.available << "\n";
    }
    out << "\n";

    write_header(out, "flexlm_feature_queued",
                 "Requests currently queued per feature");
    for (const auto& f : features) {
        std::string lbl = "feature=\"" + escape_label(f.feature) + "\","
                          "vendor=\""  + escape_label(f.vendor)  + "\"";
        out << "flexlm_feature_queued{" << lbl << "} " << f.queued << "\n";
    }
    out << "\n";

    // ── per-backend health ────────────────────────────────────────────────
    auto backends = pool_->backend_statuses();

    write_header(out, "flexlm_backend_healthy",
                 "1 if backend is reachable, 0 if unhealthy");
    for (const auto& bs : backends) {
        std::string lbl = "host=\"" + escape_label(bs.server.host) + "\","
                          "port=\"" + std::to_string(bs.server.port) + "\","
                          "name=\"" + escape_label(bs.server.name) + "\"";
        out << "flexlm_backend_healthy{" << lbl << "} "
            << (bs.healthy ? 1 : 0) << "\n";
    }
    out << "\n";

    write_header(out, "flexlm_backend_fail_streak",
                 "Consecutive failed polls for each backend");
    for (const auto& bs : backends) {
        std::string lbl = "host=\"" + escape_label(bs.server.host) + "\","
                          "port=\"" + std::to_string(bs.server.port) + "\","
                          "name=\"" + escape_label(bs.server.name) + "\"";
        out << "flexlm_backend_fail_streak{" << lbl << "} "
            << bs.fail_streak << "\n";
    }
    out << "\n";

    // ── DB-backed metrics ─────────────────────────────────────────────────
    if (tracker_) {
        // Denial counts (last 24 h)
        auto denials = tracker_->query_denials_24h();
        write_header(out, "flexlm_denials_24h",
                     "License denial count per feature in last 24 hours");
        for (const auto& d : denials) {
            std::string lbl = "feature=\"" + escape_label(d.feature) + "\"";
            out << "flexlm_denials_24h{" << lbl << "} "
                << d.denials_24h << "\n";
        }
        out << "\n";

        // Active checkout count
        auto checkouts = tracker_->query_active_checkouts();
        write_header(out, "flexlm_active_checkouts_total",
                     "Number of currently open license checkouts");
        out << "flexlm_active_checkouts_total " << checkouts.size() << "\n\n";
    }

    return out.str();
}

} // namespace api
