#pragma once

namespace dbscan {

/** @brief Cartesian coordinates in metres, stored as three float values. */
struct Point {
    float x;
    float y;
    float z;
};

/**
 * @brief Return squared Euclidean distance without a square root.
 * Coordinates are widened before subtraction and squaring so finite float
 * inputs do not overflow or underflow as they could with float intermediates.
 */
inline double dist2(const Point& a, const Point& b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace dbscan
