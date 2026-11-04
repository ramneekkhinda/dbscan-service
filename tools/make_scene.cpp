#include "xyz_io.h"
#include "synthetic.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

/**
 * @brief Write a fixed-seed 1,000-point sphere scene and companion ground-truth labels.
 * Generates three 300-point surfaces and 100 distant noise samples for validation
 * with eps=0.65 and minSamples=4. An optional argument selects the XYZ output path.
 */
int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::invalid_argument("usage: make_scene [scene.xyz]");
        std::filesystem::path path(argc == 2 ? argv[1] : "examples/data/spheres/sample_scene.xyz");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::mt19937 random(42);
        std::vector<dbscan::Point> points;
        for (int sphere = 0; sphere < 3; ++sphere) {
            synthetic::appendSphere(points, 300, {12.0F * sphere, 0, 0}, 1.0F, random);
        }
        synthetic::appendNoise(points, 100, 50.0F, 150.0F, random);
        dbscan::writeXyz(path, points);
        path.replace_extension(".labels");
        std::ofstream labels(path);
        for (int i = 0; i < 1000; ++i) labels << (i < 900 ? i / 300 + 1 : -1) << '\n';
        labels.close();
        if (!labels) throw std::runtime_error("cannot write ground truth");
        std::cout << "Wrote 1000 points and ground truth; use eps=0.65, min_samples=4\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
