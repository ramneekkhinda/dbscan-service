#include "dbscan/dbscan.h"
#include "synthetic.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

/**
 * @brief Compare naive and grid clustering time on the same fixed-seed 5,000-point scene.
 * Warm each implementation once, then average 20 complete calls, including grid
 * construction. Print cluster/noise counts; timings are measurements, not assertions.
 */
int main() {
    std::mt19937 random(42);
    std::vector<dbscan::Point> points;
    for (int sphere = 0; sphere < 10; ++sphere) {
        synthetic::appendSphere(points, 400, {10.0F * sphere, 0, 0}, 1.0F, random);
    }
    synthetic::appendNoise(points, 1000, -100.0F, 100.0F, random);
    constexpr float eps = 0.3F;
    constexpr int minSamples = 6;
    constexpr int repeats = 20;

    // Exclude scene generation, warm up once, then time complete clustering calls.
    // The grid timing includes construction of the spatial index on every call.
    std::cout << "5000 points: 10 sphere surfaces + 1000 uniform noise points\n"
              << "eps=" << eps << ", min_samples=" << minSamples
              << ", mean of " << repeats << " calls (Release recommended)\n";
    for (const bool useGrid : {false, true}) {
        const auto cluster = useGrid ? dbscan::dbscan : dbscan::dbscanNaive;
        auto labels = cluster(points, eps, minSamples);
        const auto start = std::chrono::steady_clock::now();
        for (int run = 0; run < repeats; ++run) {
            labels = cluster(points, eps, minSamples);
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / repeats;
        // Printing label statistics also makes the computed results observable.
        std::cout << (useGrid ? "grid:  " : "naive: ") << std::fixed
                  << std::setprecision(3) << ms << " ms/call, "
                  << *std::max_element(labels.begin(), labels.end()) << " clusters, "
                  << std::count(labels.begin(), labels.end(), dbscan::kNoise)
                  << " noise points\n";
    }
}
