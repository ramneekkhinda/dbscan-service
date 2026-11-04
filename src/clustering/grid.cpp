#include "dbscan/grid.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace dbscan {

Grid::Grid(const std::vector<Point>& points, float eps)
    : points_(points), eps_(eps), eps2_(static_cast<double>(eps) * eps) {
    if (!std::isfinite(eps) || eps <= 0.0F) {
        throw std::invalid_argument("grid eps must be finite and > 0");
    }
    if (points.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("too many points for uint32 grid indices");
    }
    cells_.reserve(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        cells_[key(cellFor(points[i]))].push_back(static_cast<std::uint32_t>(i));
    }
}

bool Grid::inRange(const Cell& cell) {
    for (const auto axis : cell) {
        if (axis < -kOffset || axis >= kOffset) {
            return false;
        }
    }
    return true;
}

Grid::Cell Grid::cellFor(const Point& point) const {
    Cell cell{};
    const std::array<float, 3> coordinates{point.x, point.y, point.z};
    for (std::size_t axis = 0; axis < coordinates.size(); ++axis) {
        if (!std::isfinite(coordinates[axis])) {
            throw std::invalid_argument("grid coordinates must be finite");
        }
        // Floor matters for negatives: -0.1 belongs to cell -1, not cell 0.
        // Double division avoids overflow before checking the packed-key range.
        const double value = std::floor(static_cast<double>(coordinates[axis]) / eps_);
        if (value < -kOffset || value >= kOffset) {
            throw std::out_of_range("coordinate / eps exceeds the 21-bit grid range");
        }
        cell[axis] = static_cast<std::int64_t>(value);
    }
    return cell;
}

std::uint64_t Grid::key(const Cell& cell) {
    // Each offset axis occupies 21 bits; shifting unsigned values avoids UB.
    const auto x = static_cast<std::uint64_t>(cell[0] + kOffset);
    const auto y = static_cast<std::uint64_t>(cell[1] + kOffset);
    const auto z = static_cast<std::uint64_t>(cell[2] + kOffset);
    return (x << 42) | (y << 21) | z;
}

void Grid::neighbours(std::size_t index, std::vector<std::uint32_t>& out) const {
    out.clear();
    const auto& point = points_.at(index);
    const Cell centre = cellFor(point);
    // A point within eps can only be in this cell or an immediately adjacent one.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                const Cell cell{centre[0] + dx, centre[1] + dy, centre[2] + dz};
                // At the packed range boundary, skip unrepresentable empty cells.
                if (!inRange(cell)) {
                    continue;
                }
                const auto found = cells_.find(key(cell));
                if (found == cells_.end()) {
                    continue;
                }
                for (const auto candidate : found->second) {
                    if (dist2(point, points_[candidate]) <= eps2_) {
                        out.push_back(candidate);
                    }
                }
            }
        }
    }
}

}  // namespace dbscan
