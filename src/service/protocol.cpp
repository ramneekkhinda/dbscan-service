#include "protocol.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>

namespace dbscan::wire {
namespace {
/** @brief Require IEEE-754 float32 at compile time and little-endian layout at runtime. */
void checkPlatform() {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    const std::uint16_t one = 1;
    if (*reinterpret_cast<const unsigned char*>(&one) != 1) {
        throw std::runtime_error("wire protocol requires a little-endian host");
    }
}
/** @brief Copy one scalar into a fixed-offset header field without struct padding. */
template <typename T, std::size_t N>
void put(std::array<std::uint8_t, N>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
/** @brief Copy one fixed-offset header field into a scalar without alignment assumptions. */
template <typename T, std::size_t N>
void get(const std::array<std::uint8_t, N>& bytes, std::size_t offset, T& value) {
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
}
}
RequestBytes encodeRequest(const Request& r) {
    checkPlatform();
    RequestBytes b{};
    put(b, 0, r.magic); put(b, 4, r.version); put(b, 6, r.reserved);
    put(b, 8, r.eps); put(b, 12, r.minSamples); put(b, 16, r.nPoints);
    return b;
}
Status decodeRequest(const RequestBytes& b, Request& r) {
    checkPlatform();
    get(b, 0, r.magic); get(b, 4, r.version); get(b, 6, r.reserved);
    get(b, 8, r.eps); get(b, 12, r.minSamples); get(b, 16, r.nPoints);
    if (r.magic != kMagic) return Status::BadMagic;
    if (r.version != kVersion) return Status::BadVersion;
    if (r.nPoints > kMaxPoints) return Status::TooManyPoints;
    if (r.reserved != 0 || !std::isfinite(r.eps) || r.eps <= 0 ||
        r.minSamples == 0 || r.minSamples > static_cast<unsigned>(std::numeric_limits<int>::max())) {
        return Status::BadParameters;
    }
    return Status::Ok;
}
ResponseBytes encodeResponse(const Response& r) {
    checkPlatform();
    ResponseBytes b{};
    put(b, 0, r.magic); put(b, 4, r.version);
    put(b, 6, static_cast<std::uint16_t>(r.status));
    put(b, 8, r.nPoints); put(b, 12, r.dbscanMs);
    return b;
}
bool decodeResponse(const ResponseBytes& b, Response& r) {
    checkPlatform();
    std::uint16_t status = 0;
    get(b, 0, r.magic); get(b, 4, r.version); get(b, 6, status);
    get(b, 8, r.nPoints); get(b, 12, r.dbscanMs);
    r.status = static_cast<Status>(status);
    return r.magic == kMagic && r.version == kVersion && status <= 5 &&
           r.nPoints <= kMaxPoints && std::isfinite(r.dbscanMs) && r.dbscanMs >= 0 &&
           (r.status == Status::Ok || r.nPoints == 0);
}
bool readExact(int fd, void* buffer, std::size_t size) {
    auto* bytes = static_cast<char*>(buffer);
    while (size > 0) {
        const auto n = recv(fd, bytes, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}
bool writeExact(int fd, const void* buffer, std::size_t size) {
    const auto* bytes = static_cast<const char*>(buffer);
    while (size > 0) {
        const auto n = send(fd, bytes, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        bytes += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}
bool readPoints(int fd, std::uint32_t count, std::vector<Point>& points) {
    if (count > kMaxPoints) return false;
    std::vector<float> data(static_cast<std::size_t>(count) * 3);
    if (!readExact(fd, data.data(), data.size() * sizeof(float))) return false;
    points.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        points[i] = {data[3*i], data[3*i+1], data[3*i+2]};
    }
    return true;
}
bool writePoints(int fd, const std::vector<Point>& points) {
    if (points.size() > kMaxPoints) return false;
    std::vector<float> data;
    data.reserve(points.size() * 3);
    for (const auto& p : points) data.insert(data.end(), {p.x, p.y, p.z});
    return writeExact(fd, data.data(), data.size() * sizeof(float));
}
bool readLabels(int fd, std::uint32_t count, std::vector<int>& labels) {
    if (count > kMaxPoints) return false;
    std::vector<std::int32_t> data(count);
    if (!readExact(fd, data.data(), data.size() * sizeof(std::int32_t))) return false;
    labels.assign(data.begin(), data.end());
    return true;
}
bool writeLabels(int fd, const std::vector<int>& labels) {
    if (labels.size() > kMaxPoints) return false;
    const std::vector<std::int32_t> data(labels.begin(), labels.end());
    return writeExact(fd, data.data(), data.size() * sizeof(std::int32_t));
}
Socket::~Socket() { if (fd_ >= 0) close(fd_); }
void setTimeout(int fd, int seconds) {
    const timeval timeout{seconds, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        throw std::runtime_error("could not set socket timeout");
    }
}
int listenTcp(int port) {
    if (port < 1 || port > 65535) throw std::invalid_argument("port must be in 1..65535");
    Socket listener(socket(AF_INET, SOCK_STREAM, 0));
    int reuse = 1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (listener.get() < 0 ||
        setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
        bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(listener.get(), 64) < 0) {
        throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
    }
    return listener.release();
}
int connectTcp(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    addrinfo* addresses = nullptr;
    const int error = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
    if (error != 0) throw std::runtime_error(gai_strerror(error));
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
    for (auto* a = addresses; a != nullptr; a = a->ai_next) {
        Socket connection(socket(a->ai_family, a->ai_socktype, a->ai_protocol));
        if (connection.get() >= 0 && connect(connection.get(), a->ai_addr, a->ai_addrlen) == 0) {
            return connection.release();
        }
    }
    throw std::runtime_error("could not connect to " + host + ":" + port);
}
const char* statusText(Status status) {
    switch (status) {
        case Status::Ok: return "OK";
        case Status::BadMagic: return "bad magic";
        case Status::BadVersion: return "bad version";
        case Status::BadParameters: return "bad parameters or coordinates";
        case Status::TooManyPoints: return "too many points";
        case Status::InternalError: return "internal error";
    }
    return "unknown status";
}
}  // namespace dbscan::wire
