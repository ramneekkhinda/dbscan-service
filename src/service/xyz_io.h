#pragma once
#include "dbscan/point.h"
#include <filesystem>
#include <limits>
#include <vector>

namespace dbscan {
/**
 * @brief Read finite XYZ triples, ignoring blank lines, comments, and extra columns.
 * @param path Input text file; decimal parsing uses the classic locale.
 * @param maxPoints Limit enforced before retaining an additional point.
 * @return Points in file order; an empty file returns an empty vector.
 * @throws std::runtime_error For I/O errors or malformed XYZ with its line number.
 * @throws std::length_error If a valid point would exceed maxPoints.
 */
std::vector<Point> readXyz(const std::filesystem::path& path,
    std::size_t maxPoints = std::numeric_limits<std::size_t>::max());
/**
 * @brief Overwrite path with XYZ triples using nine digits to round-trip float values.
 * An empty vector produces an empty file. Uses the classic locale and checks
 * the stream after closing so delayed write failures are reported.
 * @throws std::runtime_error If opening or writing the file fails.
 */
void writeXyz(const std::filesystem::path& path, const std::vector<Point>& points);
}
