#pragma once
#include "dbscan/point.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dbscan::wire {
inline constexpr std::uint32_t kMagic = 0x44425343;
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::uint32_t kMaxPoints = 1000000;
enum class Status : std::uint16_t {
    Ok, BadMagic, BadVersion, BadParameters, TooManyPoints, InternalError
};
struct Request {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion, reserved = 0;
    float eps = 0;
    std::uint32_t minSamples = 0, nPoints = 0;
};
struct Response {
    std::uint32_t magic = kMagic;
    std::uint16_t version = kVersion;
    Status status = Status::Ok;
    std::uint32_t nPoints = 0;
    float dbscanMs = 0;
};
using RequestBytes = std::array<std::uint8_t, 20>;
using ResponseBytes = std::array<std::uint8_t, 16>;
// Wire data is little-endian IEEE-754 float32. Reject unsupported host layouts.
RequestBytes encodeRequest(const Request& request);
Status decodeRequest(const RequestBytes& bytes, Request& request);
ResponseBytes encodeResponse(const Response& response);
bool decodeResponse(const ResponseBytes& bytes, Response& response);
bool readExact(int fd, void* buffer, std::size_t size);
bool writeExact(int fd, const void* buffer, std::size_t size);
bool readPoints(int fd, std::uint32_t count, std::vector<Point>& points);
bool writePoints(int fd, const std::vector<Point>& points);
bool readLabels(int fd, std::uint32_t count, std::vector<int>& labels);
bool writeLabels(int fd, const std::vector<int>& labels);
int listenTcp(int port);
int connectTcp(const std::string& host, const std::string& port);
void setTimeout(int fd, int seconds);
const char* statusText(Status status);

// Own one descriptor. Noncopyable so only one destructor can close it.
class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int get() const { return fd_; }
    int release() { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};
}  // namespace dbscan::wire
