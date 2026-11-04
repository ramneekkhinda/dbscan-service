#include "dbscan/dbscan.h"
#include "dbscan/grid.h"
#include "synthetic.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace {

using dbscan::Point;

// Both maps are needed: prevent either splitting or merging a cluster.
// Noise must match exactly; 0 is never a valid final label.
bool equivalentLabels(const std::vector<int>& actual, const std::vector<int>& expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    std::map<int, int> forward;
    std::map<int, int> reverse;
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const int a = actual[i];
        const int b = expected[i];
        if (a == dbscan::kNoise || b == dbscan::kNoise) {
            if (a != b) {
                return false;
            }
            continue;
        }
        if (a <= 0 || b <= 0) {
            return false;
        }
        const auto ab = forward.emplace(a, b);
        const auto ba = reverse.emplace(b, a);
        if (ab.first->second != b || ba.first->second != a) {
            return false;
        }
    }
    return true;
}

TEST(LabelComparison, AllowsRenumberingButRejectsWrongMembership) {
    EXPECT_TRUE(equivalentLabels({1, 1, -1, 2}, {7, 7, -1, 4}));
    EXPECT_FALSE(equivalentLabels({1, 1}, {1, 2}));
    EXPECT_FALSE(equivalentLabels({1, 2}, {1, 1}));
    EXPECT_FALSE(equivalentLabels({-1}, {1}));
    EXPECT_FALSE(equivalentLabels({0}, {0}));
    EXPECT_FALSE(equivalentLabels({1}, {}));
}

TEST(Point, SquaredDistance) {
    EXPECT_FLOAT_EQ(dbscan::dist2({-1, -2, -3}, {2, 2, -3}), 25.0F);
}

TEST(Dbscan, EmptyInput) {
    EXPECT_TRUE(dbscan::dbscan({}, 1.0F, 1).empty());
}

TEST(Dbscan, SinglePointCountsItself) {
    const std::vector<Point> points{{0, 0, 0}};
    EXPECT_EQ(dbscan::dbscan(points, 0.5F, 1), (std::vector<int>{1}));
    EXPECT_EQ(dbscan::dbscan(points, 0.5F, 2), (std::vector<int>{-1}));
}

TEST(Dbscan, TwoFarApartBlobs) {
    const std::vector<Point> points{{0, 0, 0}, {0.1F, 0, 0}, {0.2F, 0, 0},
                                    {10, 0, 0}, {10.1F, 0, 0}, {10.2F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, 2, 2, 2}));
}

TEST(Dbscan, BlobPlusOutlier) {
    const std::vector<Point> points{{0, 0, 0}, {0.1F, 0, 0}, {0, 0.1F, 0}, {20, 20, 20}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, -1}));
}

TEST(Dbscan, PreviouslyNoisyBorderPointDoesNotExpand) {
    // At x=0 there are only 3 neighbours, so it is initially noise.
    // x=0.9 is core and adopts x=0. The point x=-0.9 stays noise:
    // it is close to that border point but to no core point.
    const std::vector<Point> points{{0, 0, 0}, {0.9F, 0, 0}, {1.1F, 0, 0},
                                    {1.2F, 0, 0}, {-0.9F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 4), {1, 1, 1, 1, -1}));
}

TEST(Dbscan, UnvisitedBorderPointDoesNotExpand) {
    // The core is visited first this time, exercising the other border path.
    const std::vector<Point> points{{0.9F, 0, 0}, {1.1F, 0, 0}, {1.2F, 0, 0},
                                    {0, 0, 0}, {-0.9F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 4), {1, 1, 1, 1, -1}));
}

TEST(Dbscan, ExpansionReachesBeyondInitialNeighbourhood) {
    std::vector<Point> points;
    for (int x = 0; x < 20; ++x) {
        points.push_back({static_cast<float>(x), 0, 0});
    }
    // Each interior point is core; both endpoints are border points.
    // Reaching the far endpoint requires appending new seeds repeatedly.
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.01F, 3), std::vector<int>(20, 1)));
}

TEST(Dbscan, ThreeSphereSurfacesPlusUniformNoise) {
    std::mt19937 random(42);
    std::vector<Point> points;
    std::vector<int> expected;
    for (int cluster = 1; cluster <= 3; ++cluster) {
        synthetic::appendSphere(points, 300,
                                {12.0F * static_cast<float>(cluster - 1), 0, 0},
                                1.0F, random);
        expected.insert(expected.end(), 300, cluster);
    }
    // Far from the spheres and sparse relative to eps; fixed seed for repeatability.
    synthetic::appendNoise(points, 100, 50.0F, 150.0F, random);
    expected.insert(expected.end(), 100, dbscan::kNoise);
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.65F, 4), expected));
}

TEST(Dbscan, NegativeCoordinates) {
    const std::vector<Point> points{{-10, -5, -3}, {-10.1F, -5, -3}, {-10, -5.1F, -3},
                                    {-30, -30, -30}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, -1}));
}

TEST(Dbscan, IncludesExactEpsBoundary) {
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{0, 0, 0}, {3, 4, 0}}, 5.0F, 2), {1, 1}));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{0, 0, 0}, {3, 4, 0}}, 4.9F, 2), {-1, -1}));
}

TEST(Dbscan, DuplicatePointsCountSeparately) {
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{1, 2, 3}, {1, 2, 3}, {1, 2, 3}}, 0.1F, 3),
                                 {1, 1, 1}));
}

TEST(Dbscan, ClusterNumbersFollowDiscoveryOrder) {
    // Exact labels are intentional here: numbering itself is the contract tested.
    EXPECT_EQ(dbscan::dbscan({{20, 0, 0}, {-20, 0, 0}, {0, 0, 0}}, 1.0F, 1),
              (std::vector<int>{1, 2, 3}));
}

TEST(Dbscan, RejectsInvalidParametersEvenForEmptyInput) {
    for (const float eps : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_THROW(dbscan::dbscan({}, eps, 1), std::invalid_argument);
    }
    EXPECT_THROW(dbscan::dbscan({}, 1.0F, 0), std::invalid_argument);
    EXPECT_THROW(dbscan::dbscan({}, 1.0F, -1), std::invalid_argument);
}

TEST(Dbscan, NamedReferenceIsAvailable) {
    EXPECT_EQ(dbscan::dbscanNaive({{0, 0, 0}}, 1.0F, 1), (std::vector<int>{1}));
}

TEST(Grid, NeighboursMatchFullScanAcrossCellsAndReuseOutput) {
    std::vector<Point> points;
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            for (int z = -2; z <= 2; ++z) {
                points.push_back({0.6F * x, 0.6F * y, 0.6F * z});
            }
        }
    }
    points.push_back({0, 0, 0}); // A duplicate is still a separate neighbour.
    points.push_back({1, 0, 0}); // Exact eps boundary from the origin.
    const dbscan::Grid grid(points, 1.0F);
    std::vector<std::uint32_t> actual{99999};
    for (std::size_t i = 0; i < points.size(); ++i) {
        SCOPED_TRACE(i);
        std::vector<std::uint32_t> expected;
        for (std::size_t j = 0; j < points.size(); ++j) {
            if (dbscan::dist2(points[i], points[j]) <= 1.0F) {
                expected.push_back(static_cast<std::uint32_t>(j));
            }
        }
        grid.neighbours(i, actual);
        std::sort(actual.begin(), actual.end());
        EXPECT_EQ(actual, expected);
    }
}

TEST(Grid, PackedKeyLimitsKeepAxesDistinctAndDoNotWrap) {
    constexpr float low = -1048576.0F;
    constexpr float high = 1048575.0F;
    const std::vector<Point> points{{low, 0, 0}, {high, 0, 0}, {0, low, 0},
                                    {0, high, 0}, {0, 0, low}, {0, 0, high}, {0, 0, 0}};
    const dbscan::Grid grid(points, 1.0F);
    std::vector<std::uint32_t> neighbours;
    for (std::size_t i = 0; i < points.size(); ++i) {
        grid.neighbours(i, neighbours);
        EXPECT_EQ(neighbours, (std::vector<std::uint32_t>{static_cast<std::uint32_t>(i)}));
    }
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 1),
                                  dbscan::dbscanNaive(points, 1.0F, 1)));
}

TEST(Grid, RejectsCoordinatesThatCannotBePacked) {
    // Try every axis: merely masking to 21 bits would silently alias distant cells.
    for (std::size_t axis = 0; axis < 3; ++axis) {
        for (const float coordinate : {-1048577.0F, 1048576.0F}) {
            std::array<float, 3> values{0, 0, 0};
            values[axis] = coordinate;
            const std::vector<Point> points{{values[0], values[1], values[2]}};
            EXPECT_THROW(dbscan::Grid(points, 1.0F), std::out_of_range);
            EXPECT_THROW(dbscan::dbscan(points, 1.0F, 1), std::out_of_range);
        }
    }
}

TEST(Grid, RejectsNonFiniteCoordinatesAndInvalidEps) {
    const std::vector<Point> empty;
    for (const float eps : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_THROW(dbscan::Grid(empty, eps), std::invalid_argument);
    }
    for (const float value : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
        const std::vector<Point> points{{0, value, 0}};
        EXPECT_THROW(dbscan::Grid(points, 1.0F), std::invalid_argument);
    }
}

TEST(Dbscan, GridMatchesNaiveOnSeveralFixedSeedScenes) {
    for (const unsigned seed : {7U, 42U, 123U, 456U, 2026U}) {
        std::mt19937 random(seed);
        std::vector<Point> points;
        for (const auto centre : {Point{-12, -4, -2}, Point{0, 0, 0}, Point{12, 4, 2}}) {
            synthetic::appendSphere(points, 100, centre, 1.0F, random);
        }
        synthetic::appendNoise(points, 100, -20.0F, 20.0F, random);
        std::shuffle(points.begin(), points.end(), random);
        for (const float eps : {0.2F, 0.65F, 1.5F}) {
            for (const int minSamples : {1, 4, 12}) {
                SCOPED_TRACE(::testing::Message() << "seed=" << seed << " eps=" << eps
                                                  << " minSamples=" << minSamples);
                EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, eps, minSamples),
                                              dbscan::dbscanNaive(points, eps, minSamples)));
            }
        }
    }
}

TEST(Dbscan, DenseCloudMatchesNaive) {
    const std::vector<Point> points(200, Point{-0.1F, 0.1F, -0.1F});
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.5F, 4),
                                  dbscan::dbscanNaive(points, 0.5F, 4)));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.5F, 4), std::vector<int>(200, 1)));
}

TEST(Dbscan, SquaredDistancesDoNotOverflowOrUnderflowFloat) {
    for (const float eps : {1e-30F, 1e30F}) {
        const std::vector<Point> points{{0, 0, 0}, {1.5F * eps, 0, 0}};
        EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, eps, 2), {-1, -1}));
        EXPECT_TRUE(equivalentLabels(dbscan::dbscanNaive(points, eps, 2), {-1, -1}));
    }
}

TEST(Dbscan, FloatRoundingDoesNotConnectPointsOutsideRadius) {
    const std::vector<Point> points{{-1e-8F, 0, 0}, {1, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 2), {-1, -1}));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscanNaive(points, 1.0F, 2), {-1, -1}));
}

TEST(Dbscan, BothEntryPointsRejectNonFiniteCoordinates) {
    for (const float value : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
        const std::vector<Point> points{{0, 0, value}};
        EXPECT_THROW(dbscan::dbscan(points, 1.0F, 1), std::invalid_argument);
        EXPECT_THROW(dbscan::dbscanNaive(points, 1.0F, 1), std::invalid_argument);
    }
}

}  // namespace
