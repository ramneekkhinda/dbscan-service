#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace dbscan {
/** @brief Mutex-protected service counters and cumulative Prometheus duration histogram. */
class Metrics {
public:
    /** @brief Adjust the active clustering-connection gauge, normally by +1 or -1. */
    void connection(int delta);
    /**
     * @brief Record one finished handler and whether the request succeeded.
     * @param success False increments the error counter; every call counts a request.
     * @param points Points in a completed clustering run, counted when seconds >= 0.
     * @param seconds Clustering duration including grid construction. A negative
     * value means clustering did not complete: omit points and histogram updates.
     * Completed clustering is counted even if sending its response later failed.
     */
    void record(bool success, std::size_t points, double seconds);
    /** @brief Render a consistent Prometheus text snapshot while holding the mutex. */
    std::string render() const;
private:
    mutable std::mutex mutex_;
    std::uint64_t requests_ = 0, errors_ = 0, points_ = 0, samples_ = 0;
    std::int64_t connections_ = 0;
    double sum_ = 0;
    inline static constexpr std::array<double, 8> bounds_{0.001, 0.0025, 0.005, 0.01,
                                                        0.025, 0.05, 0.1, 1.0};
    std::array<std::uint64_t, 8> buckets_{};
};
}
