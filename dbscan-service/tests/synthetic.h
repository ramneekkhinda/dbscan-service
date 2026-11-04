#pragma once

#include "dbscan/point.h"

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace synthetic {

// Uniform on a sphere's surface: uniform height and angle, not latitude.
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

inline void appendNoise(std::vector<dbscan::Point>& points, std::size_t count,
                        float low, float high, std::mt19937& random) {
    std::uniform_real_distribution<float> coordinate(low, high);
    for (std::size_t i = 0; i < count; ++i) {
        points.push_back({coordinate(random), coordinate(random), coordinate(random)});
    }
}

}  // namespace synthetic
