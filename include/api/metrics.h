#pragma once
#include <string>
#include <vector>
#include <sstream>
#include "pool/pool_manager.h"
#include "tracker/usage_tracker.h"

namespace api {

// Renders a Prometheus text exposition format metrics page.
// Spec: https://prometheus.io/docs/instrumenting/exposition_formats/
//
// Metric families exposed:
//   flexlm_feature_total{feature,vendor}         gauge  total seats
//   flexlm_feature_in_use{feature,vendor}         gauge  seats in use
//   flexlm_feature_available{feature,vendor}      gauge  seats available
//   flexlm_feature_queued{feature,vendor}          gauge  queued requests
//   flexlm_backend_healthy{host,port,name}         gauge  1=up 0=down
//   flexlm_backend_fail_streak{host,port,name}     gauge  consecutive failures
//   flexlm_denials_24h{feature}                    gauge  denials in last 24 h
//   flexlm_active_checkouts_total                  gauge  open checkouts
//   flexlm_broker_info{version}                    gauge  always 1 (build info)

class MetricsRenderer {
public:
    MetricsRenderer(std::shared_ptr<pool::PoolManager>     pool,
                    std::shared_ptr<tracker::UsageTracker> tracker);

    // Render full metrics page as Prometheus text format
    std::string render() const;

private:
    std::shared_ptr<pool::PoolManager>      pool_;
    std::shared_ptr<tracker::UsageTracker>  tracker_;

    // Helpers
    static std::string escape_label(const std::string& s);

    static void write_gauge(std::ostringstream& out,
                            const std::string&  name,
                            const std::string&  help,
                            const std::string&  labels,  // pre-formatted "k=v,..."
                            double              value);

    static void write_header(std::ostringstream& out,
                             const std::string&  name,
                             const std::string&  help,
                             const std::string&  type = "gauge");
};

} // namespace api
