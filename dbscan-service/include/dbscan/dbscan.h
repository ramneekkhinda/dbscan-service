#pragma once

#include "dbscan/point.h"

#include <vector>

namespace dbscan {

inline constexpr int kNoise = -1;
inline constexpr int kUnvisited = 0;

// Labels correspond to input order: noise = -1, clusters = 1, 2, ... .
// A neighbourhood includes the point itself and the eps boundary.
// Throws std::invalid_argument unless eps is finite and > 0 and minSamples >= 1.
// Input coordinates must be finite. Squared distances use double intermediates.
std::vector<int> dbscanNaive(const std::vector<Point>& pts, float eps, int minSamples);

// Public entry point using the eps-sized uniform grid. Cell indices must fit
// [-2^20, 2^20 - 1] per axis; throws std::out_of_range otherwise.
// Both entry points require the point count to fit in int cluster labels.
std::vector<int> dbscan(const std::vector<Point>& pts, float eps, int minSamples);

}  // namespace dbscan
