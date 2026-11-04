#pragma once

#include "dbscan/point.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dbscan {

class Grid {
public:
    // Points must outlive the grid and must not be modified while it is in use.
    // Cell size is eps. Coordinates must be finite and their cell indices must
    // fit in [-2^20, 2^20 - 1]; otherwise construction throws.
    Grid(const std::vector<Point>& points, float eps);

    // Clears and reuses out. Includes the query point and the eps boundary.
    void neighbours(std::size_t index, std::vector<std::uint32_t>& out) const;

private:
    using Cell = std::array<std::int64_t, 3>;
    static constexpr std::int64_t kOffset = std::int64_t{1} << 20;
    static bool inRange(const Cell& cell);
    static std::uint64_t key(const Cell& cell);
    Cell cellFor(const Point& point) const;

    const std::vector<Point>& points_;
    float eps_;
    double eps2_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
};

}  // namespace dbscan
