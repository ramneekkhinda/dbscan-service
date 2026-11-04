#pragma once

#include "dbscan/point.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dbscan {

/** @brief Spatial index grouping points into eps-sized cells for radius queries. */
class Grid {
public:
    /**
     * @brief Index input points by floor(coordinate / eps) on each axis.
     * @param points Referenced input; it must outlive the grid and stay unchanged.
     * @param eps Finite, positive cell size and neighbor-search radius.
     * @throws std::invalid_argument For invalid eps or nonfinite coordinates.
     * @throws std::length_error If point indices cannot fit in uint32_t.
     * @throws std::out_of_range If cell indices exceed [-2^20, 2^20 - 1].
     */
    Grid(const std::vector<Point>& points, float eps);

    /**
     * @brief Collect indices within eps using nearby cells and an exact distance check.
     * @param index Query point index in the original input.
     * @param[out] out Cleared and reused; includes self and the eps boundary.
     * Result order follows cell traversal, not input order. Outward-rounded cell
     * bounds keep candidate selection consistent with the double distance test.
     * @throws std::out_of_range If index is outside the input vector.
     */
    void neighbours(std::size_t index, std::vector<std::uint32_t>& out) const;

private:
    using Cell = std::array<std::int64_t, 3>;
    static constexpr std::int64_t kOffset = std::int64_t{1} << 20;
    /** @brief Check that all three cell indices fit the packed-key range. */
    static bool inRange(const Cell& cell);
    /** @brief Pack an in-range cell into three unsigned 21-bit axis fields. */
    static std::uint64_t key(const Cell& cell);
    /** @brief Compute a cell using floor; reject nonfinite or unpackable coordinates. */
    Cell cellFor(const Point& point) const;

    const std::vector<Point>& points_;
    float eps_;
    double eps2_;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> cells_;
};

}  // namespace dbscan
