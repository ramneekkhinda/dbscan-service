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
/**
 * @brief Serialize the 20-byte request header without copying struct padding.
 * Header codecs require a little-endian IEEE-754 float32 host. Encoding does
 * not validate field values; use decodeRequest() to check them.
 */
RequestBytes encodeRequest(const Request& request);
/** @brief Populate request and return its validation status before payload allocation. */
Status decodeRequest(const RequestBytes& bytes, Request& request);
/** @brief Serialize the 16-byte response header without validating its fields. */
ResponseBytes encodeResponse(const Response& response);
/**
 * @brief Populate response and validate magic, version, status, count, and duration.
 * @return False for malformed fields, including nonzero payload counts on errors.
 */
bool decodeResponse(const ResponseBytes& bytes, Response& response);
/**
 * @brief Receive exactly size bytes, retrying interrupted and partial reads.
 * @return False on EOF or a socket error; buffer may then contain partial data.
 */
bool readExact(int fd, void* buffer, std::size_t size);
/**
 * @brief Send exactly size bytes, retrying interrupted and partial writes.
 * @return False on a socket error. MSG_NOSIGNAL prevents peer closure from
 * terminating the process; bytes already sent cannot be rolled back.
 */
bool writeExact(int fd, const void* buffer, std::size_t size);
/**
 * @brief Read count XYZ triples as float32 values into points.
 * @return False for an excessive count or incomplete payload. Coordinate
 * finiteness is checked later by clustering, not by this transport helper.
 */
bool readPoints(int fd, std::uint32_t count, std::vector<Point>& points);
/** @brief Send XYZ float32 triples; return false for excessive count or I/O failure. */
bool writePoints(int fd, const std::vector<Point>& points);
/**
 * @brief Read count signed int32 labels; return false for excessive count or I/O failure.
 * Label meaning and its agreement with the request count are checked by the client.
 */
bool readLabels(int fd, std::uint32_t count, std::vector<int>& labels);
/** @brief Send signed int32 labels; return false for excessive count or I/O failure. */
bool writeLabels(int fd, const std::vector<int>& labels);
/**
 * @brief Listen on all IPv4 interfaces at port; caller owns the returned descriptor.
 * @throws std::invalid_argument If port is outside 1..65535.
 * @throws std::runtime_error If socket setup, binding, or listening fails.
 */
int listenTcp(int port);
/**
 * @brief Resolve host and try its addresses until a TCP connection succeeds.
 * @return Connected descriptor owned by the caller, normally wrapped in Socket.
 * @throws std::runtime_error If resolution or every connection attempt fails.
 */
int connectTcp(const std::string& host, const std::string& port);
/**
 * @brief Set receive and send timeouts in seconds on an existing socket.
 * @throws std::runtime_error If either socket option cannot be set.
 */
void setTimeout(int fd, int seconds);
/** @brief Return a diagnostic string for a protocol status, including unknown values. */
const char* statusText(Status status);

/** @brief Noncopyable owner that closes its descriptor at scope exit unless released. */
class Socket {
public:
    /** @brief Take ownership of fd without duplicating the descriptor. */
    explicit Socket(int fd) : fd_(fd) {}
    /** @brief Close the owned descriptor if it is nonnegative. */
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    /** @brief Borrow the descriptor without transferring ownership. */
    int get() const { return fd_; }
    /** @brief Transfer ownership to the caller and disable closing by this object. */
    int release() { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};
}  // namespace dbscan::wire
