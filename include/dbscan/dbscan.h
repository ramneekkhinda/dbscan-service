#pragma once

#include "dbscan/point.h"

#include <vector>

namespace dbscan {

inline constexpr int kNoise = -1;
inline constexpr int kUnvisited = 0;

/**
 * @brief Cluster points with a full-scan reference search for tests and benchmarks.
 * @param pts Finite XYZ coordinates; squared distances use double intermediates.
 * @param eps Finite radius greater than zero, in the same units as the coordinates.
 * @param minSamples Minimum neighborhood size, including the point itself.
 * @return Labels in input order: noise = -1, clusters = 1, 2, ... in discovery order.
 * Points exactly eps apart are neighbors. Empty input returns an empty vector
 * after parameter validation. Time is O(n^2); additional memory is O(n).
 * @throws std::invalid_argument For invalid parameters or nonfinite coordinates.
 * @throws std::length_error If the point count cannot fit in int cluster labels.
 */
std::vector<int> dbscanNaive(const std::vector<Point>& pts, float eps, int minSamples);

/**
 * @brief Cluster points using an eps-sized uniform grid; used by the server.
 * @param pts Finite XYZ coordinates, kept unchanged.
 * @param eps Finite radius greater than zero; also the grid cell size.
 * @param minSamples Minimum neighborhood size, including the point itself.
 * @return Labels in input order with the same conventions as dbscanNaive().
 * Shares cluster expansion with the reference, but narrows neighbor candidates
 * through the grid. Uses O(n) additional memory; worst-case time remains O(n^2).
 * @throws std::invalid_argument For invalid parameters or nonfinite coordinates.
 * @throws std::length_error If the point count cannot fit in int cluster labels.
 * @throws std::out_of_range If a cell index is outside [-2^20, 2^20 - 1].
 */
std::vector<int> dbscan(const std::vector<Point>& pts, float eps, int minSamples);

}  // namespace dbscan
