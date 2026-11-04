#include "dbscan/dbscan.h"
#include "metrics.h"
#include "protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace wire = dbscan::wire;
namespace {
bool processRequest(int fd, std::size_t& count, double& seconds, bool& responseStarted) {
    wire::RequestBytes bytes{};
    if (!wire::readExact(fd, bytes.data(), bytes.size())) return false;
    wire::Request request;
    wire::Response response;
    response.status = wire::decodeRequest(bytes, request);
    std::vector<int> labels;
    if (response.status == wire::Status::Ok) {
        std::vector<dbscan::Point> points;
        if (!wire::readPoints(fd, request.nPoints, points)) return false;
        try {
            const auto start = std::chrono::steady_clock::now();
            labels = dbscan::dbscan(points, request.eps, static_cast<int>(request.minSamples));
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            count = points.size();
            response.nPoints = request.nPoints;
            response.dbscanMs = static_cast<float>(seconds * 1000);
        } catch (const std::invalid_argument&) {
            response.status = wire::Status::BadParameters;
        } catch (const std::out_of_range&) {
            response.status = wire::Status::BadParameters;
        }
    }
    const auto reply = wire::encodeResponse(response);
    responseStarted = true;
    if (!wire::writeExact(fd, reply.data(), reply.size())) return false;
    return response.status == wire::Status::Ok && wire::writeLabels(fd, labels);
}
void handleClient(int fd, const std::shared_ptr<dbscan::Metrics>& metrics) {
    const wire::Socket client(fd);
    metrics->connection(1);
    bool success = false;
    bool responseStarted = false;
    std::size_t count = 0;
    double seconds = -1;
    try {
        wire::setTimeout(fd, 30);
        success = processRequest(fd, count, seconds, responseStarted);
    } catch (const std::exception& error) {
        std::cerr << "request: " << error.what() << '\n';
        // Once a header starts, another header would be mistaken for label bytes.
        // Close an incomplete response instead; the client detects truncation.
        if (!responseStarted) {
            wire::Response response;
            response.status = wire::Status::InternalError;
            const auto bytes = wire::encodeResponse(response);
            wire::writeExact(fd, bytes.data(), bytes.size());
        }
    }
    metrics->record(success, count, seconds);
    metrics->connection(-1);
}
void metricsLoop(int listener, const std::shared_ptr<dbscan::Metrics>& metrics) {
    for (;;) {
        const wire::Socket client(accept(listener, nullptr, nullptr));
        if (client.get() < 0) {
            if (errno == EINTR) continue;
            std::cerr << "metrics accept failed\n";
            return;
        }
        try {
            wire::setTimeout(client.get(), 5);
            std::string request;
            char buffer[1024];
            while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
                const auto available = std::min(sizeof(buffer), std::size_t{8192} - request.size());
                const auto n = recv(client.get(), buffer, available, 0);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                request.append(buffer, static_cast<std::size_t>(n));
            }
            const bool valid = request.find("\r\n\r\n") != std::string::npos;
            const bool tooLarge = !valid && request.size() == 8192;
            const bool found = valid && (request.rfind("GET /metrics HTTP/1.1\r\n", 0) == 0 ||
                                         request.rfind("GET /metrics HTTP/1.0\r\n", 0) == 0);
            const std::string body = found ? metrics->render() : "request rejected\n";
            const auto status = found ? "200 OK" : tooLarge ? "431 Request Header Fields Too Large" :
                                valid ? "404 Not Found" : "400 Bad Request";
            const std::string reply = std::string("HTTP/1.1 ") + status +
                "\r\nContent-Type: text/plain; version=0.0.4; charset=utf-8\r\nContent-Length: " +
                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
            wire::writeExact(client.get(), reply.data(), reply.size());
        } catch (const std::exception& error) {
            std::cerr << "metrics: " << error.what() << '\n';
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        int port = 5555, metricsPort = 9091;
        for (int i = 1; i < argc; i += 2) {
            const std::string option(argv[i]);
            if (i + 1 >= argc || (option != "--port" && option != "--metrics-port")) {
                throw std::invalid_argument("usage: dbscan_server --port 5555 --metrics-port 9091");
            }
            const std::string value(argv[i+1]);
            std::size_t used = 0;
            const int number = std::stoi(value, &used);
            if (used != value.size() || number < 1 || number > 65535) {
                throw std::invalid_argument("port must be in 1..65535");
            }
            (option == "--port" ? port : metricsPort) = number;
        }
        const wire::Socket listener(wire::listenTcp(port));
        const wire::Socket exporter(wire::listenTcp(metricsPort));
        const auto metrics = std::make_shared<dbscan::Metrics>();
        std::thread(metricsLoop, exporter.get(), metrics).detach();
        std::cout << "DBSCAN on " << port << ", /metrics on " << metricsPort << std::endl;
        for (;;) {
            const int fd = accept(listener.get(), nullptr, nullptr);
            if (fd < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("accept failed");
            }
            try {
                // Copy fd into the thread: a reference to the loop variable would dangle.
                std::thread(handleClient, fd, metrics).detach();
            } catch (const std::exception& error) {
                close(fd);
                std::cerr << "start handler: " << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
