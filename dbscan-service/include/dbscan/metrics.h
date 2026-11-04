#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace dbscan {
class Metrics {
public:
    void connection(int delta);
    // seconds < 0 means clustering did not complete; omit its histogram sample.
    void record(bool success, std::size_t points, double seconds);
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
