#include "dbscan/protocol.h"
#include "dbscan/xyz_io.h"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace wire = dbscan::wire;
int main(int argc, char** argv) {
    try {
        std::string input, output, endpoint;
        wire::Request request;
        for (int i = 1; i < argc; ++i) {
            const std::string option(argv[i]);
            if (option.rfind("--", 0) != 0) {
                if (!endpoint.empty()) throw std::invalid_argument("multiple endpoints");
                endpoint = option;
                continue;
            }
            if (++i >= argc) throw std::invalid_argument("missing option value");
            const std::string value(argv[i]);
            std::size_t used = 0;
            if (option == "--input") input = value;
            else if (option == "--output-dir") output = value;
            else if (option == "--eps") {
                request.eps = std::stof(value, &used);
                if (used != value.size()) throw std::invalid_argument("invalid eps");
            } else if (option == "--min_samples") {
                const int minimum = std::stoi(value, &used);
                if (used != value.size() || minimum < 1) throw std::invalid_argument("invalid min_samples");
                request.minSamples = static_cast<std::uint32_t>(minimum);
            } else throw std::invalid_argument("unknown option: " + option);
        }
        const auto colon = endpoint.rfind(':');
        if (input.empty() || output.empty() || colon == std::string::npos || colon == 0) {
            throw std::invalid_argument("usage: processPointcloud --input input.xyz --output-dir out_dir "
                                        "--eps 0.5 --min_samples 4 host:port");
        }
        const std::string port = endpoint.substr(colon + 1);
        std::size_t used = 0;
        const int portNumber = std::stoi(port, &used);
        if (used != port.size() || portNumber < 1 || portNumber > 65535) {
            throw std::invalid_argument("port must be in 1..65535");
        }
        const auto points = dbscan::readXyz(input, wire::kMaxPoints);
        request.nPoints = static_cast<std::uint32_t>(points.size());
        const auto bytes = wire::encodeRequest(request);
        wire::Request checked;
        if (wire::decodeRequest(bytes, checked) != wire::Status::Ok) {
            throw std::invalid_argument("invalid DBSCAN parameters");
        }
        std::filesystem::create_directories(output);
        if (!std::filesystem::is_empty(output)) throw std::runtime_error("output directory must be empty");
        const wire::Socket connection(wire::connectTcp(endpoint.substr(0, colon), port));
        wire::setTimeout(connection.get(), 30);
        wire::ResponseBytes reply{};
        if (!wire::writeExact(connection.get(), bytes.data(), bytes.size()) ||
            !wire::writePoints(connection.get(), points) ||
            !wire::readExact(connection.get(), reply.data(), reply.size())) {
            throw std::runtime_error("connection failed while exchanging request");
        }
        wire::Response response;
        if (!wire::decodeResponse(reply, response)) throw std::runtime_error("invalid response header");
        if (response.status != wire::Status::Ok) throw std::runtime_error(wire::statusText(response.status));
        std::vector<int> labels;
        if (response.nPoints != points.size() ||
            !wire::readLabels(connection.get(), response.nPoints, labels)) {
            throw std::runtime_error("incomplete or mismatched response labels");
        }
        std::map<int, std::vector<dbscan::Point>> groups;
        groups[-1]; // Always write noise.xyz, even when it is empty.
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (labels[i] != -1 && (labels[i] < 1 || labels[i] > static_cast<int>(points.size()))) {
                throw std::runtime_error("invalid cluster label");
            }
            groups[labels[i]].push_back(points[i]);
        }
        for (const auto& group : groups) {
            const auto name = group.first == -1 ? "noise.xyz" : "cluster_" + std::to_string(group.first) + ".xyz";
            dbscan::writeXyz(std::filesystem::path(output) / name, group.second);
        }
        std::cout << points.size() << " points, " << groups.size() - 1 << " clusters, "
                  << groups[-1].size() << " noise points; DBSCAN " << response.dbscanMs << " ms\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
