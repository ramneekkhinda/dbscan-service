#include "dbscan/metrics.h"
#include <iomanip>
#include <locale>
#include <sstream>

namespace dbscan {
void Metrics::connection(int delta) {
    const std::lock_guard<std::mutex> lock(mutex_);
    connections_ += delta;
}
void Metrics::record(bool success, std::size_t points, double seconds) {
    const std::lock_guard<std::mutex> lock(mutex_);
    ++requests_;
    if (!success) ++errors_;
    if (seconds < 0) return;
    points_ += points;
    ++samples_;
    sum_ += seconds;
    // Prometheus histogram buckets are cumulative, including the boundary.
    for (std::size_t i = 0; i < bounds_.size(); ++i) {
        if (seconds <= bounds_[i]) ++buckets_[i];
    }
}
std::string Metrics::render() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(12);
    out << "# HELP dbscan_requests_total Completed connection handlers.\n"
        << "# TYPE dbscan_requests_total counter\ndbscan_requests_total " << requests_ << '\n'
        << "# HELP dbscan_errors_total Failed requests including transport errors.\n"
        << "# TYPE dbscan_errors_total counter\ndbscan_errors_total " << errors_ << '\n'
        << "# HELP dbscan_points_total Points in completed clustering runs.\n"
        << "# TYPE dbscan_points_total counter\ndbscan_points_total " << points_ << '\n'
        << "# HELP dbscan_active_connections Current clustering connections.\n"
        << "# TYPE dbscan_active_connections gauge\ndbscan_active_connections " << connections_ << '\n'
        << "# HELP dbscan_duration_seconds Clustering time including grid construction.\n"
        << "# TYPE dbscan_duration_seconds histogram\n";
    for (std::size_t i = 0; i < bounds_.size(); ++i) {
        out << "dbscan_duration_seconds_bucket{le=\"" << bounds_[i] << "\"} " << buckets_[i] << '\n';
    }
    out << "dbscan_duration_seconds_bucket{le=\"+Inf\"} " << samples_ << '\n'
        << "dbscan_duration_seconds_sum " << sum_ << '\n'
        << "dbscan_duration_seconds_count " << samples_ << '\n';
    return out.str();
}
}
