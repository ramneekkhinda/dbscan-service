#pragma once
#include "dbscan/point.h"
#include <filesystem>
#include <limits>
#include <vector>

namespace dbscan {
// Stop before retaining more than maxPoints, instead of loading then checking.
std::vector<Point> readXyz(const std::filesystem::path& path,
    std::size_t maxPoints = std::numeric_limits<std::size_t>::max());
void writeXyz(const std::filesystem::path& path, const std::vector<Point>& points);
}
