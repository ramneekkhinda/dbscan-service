#include "xyz_io.h"
#include <cmath>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>

namespace dbscan {
std::vector<Point> readXyz(const std::filesystem::path& path, std::size_t maxPoints) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    std::vector<Point> points;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#') continue;
        std::istringstream row(line);
        row.imbue(std::locale::classic());
        Point p{};
        if (!(row >> p.x >> p.y >> p.z) || !std::isfinite(p.x) ||
            !std::isfinite(p.y) || !std::isfinite(p.z)) {
            throw std::runtime_error("malformed XYZ at line " + std::to_string(lineNumber));
        }
        // Reject a partial third number such as '3oops'; extra columns are fine.
        const int next = row.peek();
        if (next != std::char_traits<char>::eof() &&
            !std::isspace(static_cast<unsigned char>(next))) {
            throw std::runtime_error("malformed XYZ at line " + std::to_string(lineNumber));
        }
        if (points.size() == maxPoints) {
            throw std::length_error("XYZ point limit exceeded at line " + std::to_string(lineNumber));
        }
        points.push_back(p);
    }
    if (input.bad()) throw std::runtime_error("error reading " + path.string());
    return points;
}
void writeXyz(const std::filesystem::path& path, const std::vector<Point>& points) {
    std::ofstream output(path);
    if (!output) throw std::runtime_error("cannot write " + path.string());
    output.imbue(std::locale::classic());
    output << std::setprecision(9);
    for (const auto& p : points) output << p.x << ' ' << p.y << ' ' << p.z << '\n';
    output.close();
    if (!output) throw std::runtime_error("error writing " + path.string());
}
}
