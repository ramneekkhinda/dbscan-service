#include "dbscan/dbscan.h"
#include "dbscan/grid.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace dbscan {
namespace {

// Reuse the caller's buffer; every search examines every input point.
void findNeighbours(const std::vector<Point>& pts, std::size_t index, double eps2,
                    std::vector<std::uint32_t>& neighbours) {
    neighbours.clear();
    for (std::size_t j = 0; j < pts.size(); ++j) {
        if (dist2(pts[index], pts[j]) <= eps2) {
            neighbours.push_back(static_cast<std::uint32_t>(j));
        }
    }
}

void validateParameters(const std::vector<Point>& pts, float eps, int minSamples) {
    if (!std::isfinite(eps) || eps <= 0.0F || minSamples < 1) {
        throw std::invalid_argument("eps must be finite and > 0; minSamples must be >= 1");
    }
    // Cluster IDs use int, and neighbour indices use uint32_t.
    if (pts.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error("too many points for int cluster labels");
    }
    for (const auto& point : pts) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
            throw std::invalid_argument("coordinates must be finite");
        }
    }
}

// One expansion algorithm, with the neighbour search supplied by a small lambda.
// A template lets the compiler inline either search without virtual dispatch.
template <typename Search>
std::vector<int> clusterPoints(const std::vector<Point>& pts, int minSamples, Search search) {
    const auto minimum = static_cast<std::size_t>(minSamples);
    std::vector<int> labels(pts.size(), kUnvisited);
    std::vector<std::uint32_t> neighbours;
    std::vector<std::size_t> seeds;
    // Each point can enter a seed list at most once during the entire run.
    // This bounds queue memory to O(n), even when all points are neighbours.
    std::vector<bool> queued(pts.size(), false);
    int cluster = 0;

    for (std::size_t i = 0; i < pts.size(); ++i) {
        if (labels[i] != kUnvisited) {
            continue;
        }
        search(i, neighbours);
        if (neighbours.size() < minimum) {
            labels[i] = kNoise;
            continue;
        }

        ++cluster;
        labels[i] = cluster;
        seeds.clear();
        seeds.push_back(i);
        queued[i] = true;
        for (const auto neighbour : neighbours) {
            if (!queued[neighbour]) {
                seeds.push_back(neighbour);
                queued[neighbour] = true;
            }
        }

        // Walk by index: push_back may reallocate seeds and invalidate iterators.
        for (std::size_t next = 0; next < seeds.size(); ++next) {
            const auto point = seeds[next];
            if (labels[point] == kNoise) {
                // Already examined and non-core: adopt as border, never expand.
                labels[point] = cluster;
                continue;
            }
            if (labels[point] != kUnvisited) {
                continue;
            }
            labels[point] = cluster;
            search(point, neighbours);
            if (neighbours.size() >= minimum) {
                for (const auto neighbour : neighbours) {
                    if (!queued[neighbour] && labels[neighbour] <= kUnvisited) {
                        seeds.push_back(neighbour);
                        queued[neighbour] = true;
                    }
                }
            }
        }
    }
    return labels;
}

}  // namespace

std::vector<int> dbscanNaive(const std::vector<Point>& pts, float eps, int minSamples) {
    validateParameters(pts, eps, minSamples);
    const double eps2 = static_cast<double>(eps) * eps;
    return clusterPoints(pts, minSamples,
        [&](std::size_t i, std::vector<std::uint32_t>& out) {
            findNeighbours(pts, i, eps2, out);
        });
}

std::vector<int> dbscan(const std::vector<Point>& pts, float eps, int minSamples) {
    validateParameters(pts, eps, minSamples);
    const Grid grid(pts, eps);
    return clusterPoints(pts, minSamples,
        [&](std::size_t i, std::vector<std::uint32_t>& out) {
            grid.neighbours(i, out);
        });
}

}  // namespace dbscan
