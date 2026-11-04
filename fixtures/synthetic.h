#pragma once

#include "dbscan/point.h"

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace synthetic {

/**
 * @brief Append count points sampled uniformly on a sphere surface, not its volume.
 * Uses uniform height and angle to avoid crowding the poles. The caller supplies
 * and advances the random engine so fixed seeds make tests and benchmarks repeatable.
 */
inline void appendSphere(std::vector<dbscan::Point>& points, std::size_t count,
                         dbscan::Point centre, float radius, std::mt19937& random) {
    std::uniform_real_distribution<float> height(-1.0F, 1.0F);
    std::uniform_real_distribution<float> angle(0.0F, 6.283185307F);
    for (std::size_t i = 0; i < count; ++i) {
        const float z = height(random);
        const float phi = angle(random);
        const float ring = std::sqrt(1.0F - z * z);
        points.push_back({centre.x + radius * ring * std::cos(phi),
                          centre.y + radius * ring * std::sin(phi),
                          centre.z + radius * z});
    }
}

/**
 * @brief Append count points with independently uniform XYZ coordinates in [low, high).
 * These are geometric samples; whether they are DBSCAN noise depends on the scene
 * density and clustering parameters. Existing points are preserved.
 */
inline void appendNoise(std::vector<dbscan::Point>& points, std::size_t count,
                        float low, float high, std::mt19937& random) {
    std::uniform_real_distribution<float> coordinate(low, high);
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back({coordinate(random), coordinate(random), coordinate(random)});
    }
}

}  // namespace synthetic
