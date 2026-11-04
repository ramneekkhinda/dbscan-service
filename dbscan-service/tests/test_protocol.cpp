#include "dbscan/protocol.h"
#include "dbscan/metrics.h"
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <thread>
#include <limits>

namespace wire = dbscan::wire;
TEST(Protocol, RequestRoundTripAndKnownLittleEndianBytes) {
    wire::Request request;
    request.eps = 0.5F; request.minSamples = 4; request.nPoints = 123;
    const auto bytes = wire::encodeRequest(request);
    const wire::RequestBytes expected{0x43,0x53,0x42,0x44,1,0,0,0,0,0,0,0x3f,4,0,0,0,123,0,0,0};
    EXPECT_EQ(bytes, expected);
    wire::Request decoded;
    EXPECT_EQ(wire::decodeRequest(bytes, decoded), wire::Status::Ok);
    EXPECT_FLOAT_EQ(decoded.eps, request.eps);
    EXPECT_EQ(decoded.minSamples, 4U);
    EXPECT_EQ(decoded.nPoints, 123U);
}
TEST(Protocol, RejectsInvalidRequestHeaders) {
    wire::Request request, decoded;
    request.eps = 1; request.minSamples = 1;
    request.magic = 0;
    EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::BadMagic);
    request.magic = wire::kMagic; request.version = 2;
    EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::BadVersion);
    request.version = 1; request.nPoints = wire::kMaxPoints + 1;
    EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::TooManyPoints);
    request.nPoints = 0; request.eps = 0;
    EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::BadParameters);
    request.eps = 1; request.reserved = 1;
    EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::BadParameters);
}
TEST(Protocol, ResponseRoundTripAndValidation) {
    wire::Response response, decoded;
    response.nPoints = 12; response.dbscanMs = 2.5F;
    EXPECT_TRUE(wire::decodeResponse(wire::encodeResponse(response), decoded));
    EXPECT_EQ(decoded.nPoints, 12U);
    EXPECT_FLOAT_EQ(decoded.dbscanMs, 2.5F);
    response.magic = 0;
    EXPECT_FALSE(wire::decodeResponse(wire::encodeResponse(response), decoded));
    response.magic = wire::kMagic; response.version = 2;
    EXPECT_FALSE(wire::decodeResponse(wire::encodeResponse(response), decoded));
    response.version = 1; response.nPoints = wire::kMaxPoints + 1;
    EXPECT_FALSE(wire::decodeResponse(wire::encodeResponse(response), decoded));
    response.nPoints = 0; response.status = wire::Status::BadParameters;
    EXPECT_TRUE(wire::decodeResponse(wire::encodeResponse(response), decoded));
}
TEST(Protocol, ExtremeFiniteEpsMatchesDoubleDistanceContract) {
    wire::Request request, decoded;
    request.minSamples = 1;
    for (const float eps : {1e-30F, 1e30F}) {
        request.eps = eps;
        EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::Ok);
    }
    for (const float eps : {std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN(), -1.0F}) {
        request.eps = eps;
        EXPECT_EQ(wire::decodeRequest(wire::encodeRequest(request), decoded), wire::Status::BadParameters);
    }
}
TEST(Protocol, ExactIoHandlesLargeTransfersAndPeerClosure) {
    int descriptors[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    const wire::Socket reader(descriptors[0]), writer(descriptors[1]);
    int small = 1024;
    ASSERT_EQ(setsockopt(writer.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)), 0);
    const std::string sent(1024 * 1024, 'x');
    bool written = false;
    std::thread producer([&] {
        written = wire::writeExact(writer.get(), sent.data(), sent.size());
        shutdown(writer.get(), SHUT_WR);
    });
    std::string received(sent.size(), '\0');
    EXPECT_TRUE(wire::readExact(reader.get(), received.data(), received.size()));
    producer.join();
    EXPECT_TRUE(written);
    EXPECT_EQ(sent, received);
    char byte;
    EXPECT_FALSE(wire::readExact(reader.get(), &byte, 1));
    shutdown(reader.get(), SHUT_RDWR);
    EXPECT_FALSE(wire::writeExact(writer.get(), &byte, 1)); // MSG_NOSIGNAL prevents process death.
}
TEST(Protocol, PointAndLabelPayloadsRoundTrip) {
    int descriptors[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors), 0);
    const wire::Socket a(descriptors[0]), b(descriptors[1]);
    const std::vector<dbscan::Point> points{{-1.25F, 2, 3}, {4, 5, 6}};
    ASSERT_TRUE(wire::writePoints(a.get(), points));
    std::vector<dbscan::Point> decoded;
    ASSERT_TRUE(wire::readPoints(b.get(), 2, decoded));
    EXPECT_FLOAT_EQ(decoded[0].x, -1.25F);
    EXPECT_FLOAT_EQ(decoded[1].z, 6);
    ASSERT_TRUE(wire::writeLabels(a.get(), {-1, 2}));
    std::vector<int> labels;
    ASSERT_TRUE(wire::readLabels(b.get(), 2, labels));
    EXPECT_EQ(labels, (std::vector<int>{-1, 2}));
}
TEST(Metrics, ConcurrentUpdatesAndCumulativeHistogram) {
    dbscan::Metrics metrics;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) threads.emplace_back([&] {
        metrics.connection(1);
        for (int i = 0; i < 100; ++i) metrics.record(true, 5, 0.005);
        metrics.connection(-1);
    });
    for (auto& thread : threads) thread.join();
    metrics.record(false, 0, -1);
    const auto text = metrics.render();
    for (const auto* sample : {"dbscan_requests_total 401\n", "dbscan_errors_total 1\n",
            "dbscan_active_connections 0\n", "dbscan_points_total 2000\n",
            "dbscan_duration_seconds_bucket{le=\"0.001\"} 0\n",
            "dbscan_duration_seconds_bucket{le=\"0.005\"} 400\n",
            "dbscan_duration_seconds_bucket{le=\"+Inf\"} 400\n",
            "dbscan_duration_seconds_count 400\n"}) {
        EXPECT_NE(text.find(sample), std::string::npos) << sample;
    }
}
