#pragma once

namespace dbscan {

// Cartesian coordinates in metres.
struct Point {
    float x;
    float y;
    float z;
};

// Keep float coordinates, but widen before subtracting/squaring: finite float
// coordinates can overflow or underflow when distances are computed in float.
inline double dist2(const Point& a, const Point& b) {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return dx * dx + dy * dy + dz * dz;
}

}  // namespace dbscan
