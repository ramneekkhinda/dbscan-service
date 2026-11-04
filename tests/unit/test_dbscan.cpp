#include "dbscan/dbscan.h"
#include "dbscan/grid.h"
#include "synthetic.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace {

using dbscan::Point;

/**
 * @brief Compare cluster membership while allowing positive cluster IDs to differ.
 * @param actual Labels produced by the implementation under test.
 * @param expected Known labels or labels from the naive reference.
 * @return True only for a one-to-one mapping of clusters with identical noise.
 * Both mapping directions are checked to catch splits and merges. Zero is an
 * unfinished label and is rejected even when present in both vectors.
 */
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

/**
 * @test Accept cluster renumbering, but reject splits, merges, noise changes, zero labels, and
 * size mismatches.
 */
TEST(LabelComparison, AllowsRenumberingButRejectsWrongMembership) {
    EXPECT_TRUE(equivalentLabels({1, 1, -1, 2}, {7, 7, -1, 4}));
    EXPECT_FALSE(equivalentLabels({1, 1}, {1, 2}));
    EXPECT_FALSE(equivalentLabels({1, 2}, {1, 1}));
    EXPECT_FALSE(equivalentLabels({-1}, {1}));
    EXPECT_FALSE(equivalentLabels({0}, {0}));
    EXPECT_FALSE(equivalentLabels({1}, {}));
}

/**
 * @test Check the squared Euclidean distance for a known displacement with negative coordinates.
 */
TEST(Point, SquaredDistance) {
    EXPECT_FLOAT_EQ(dbscan::dist2({-1, -2, -3}, {2, 2, -3}), 25.0F);
}

/** @test Return no labels when the input contains no points. */
TEST(Dbscan, EmptyInput) {
    EXPECT_TRUE(dbscan::dbscan({}, 1.0F, 1).empty());
}

/** @test Count the query point itself: one sample forms a cluster only when minSamples is one. */
TEST(Dbscan, SinglePointCountsItself) {
    const std::vector<Point> points{{0, 0, 0}};
    EXPECT_EQ(dbscan::dbscan(points, 0.5F, 1), (std::vector<int>{1}));
    EXPECT_EQ(dbscan::dbscan(points, 0.5F, 2), (std::vector<int>{-1}));
}

/** @test Keep two dense, widely separated groups in distinct clusters. */
TEST(Dbscan, TwoFarApartBlobs) {
    const std::vector<Point> points{{0, 0, 0}, {0.1F, 0, 0}, {0.2F, 0, 0},
                                    {10, 0, 0}, {10.1F, 0, 0}, {10.2F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, 2, 2, 2}));
}

/** @test Cluster a dense group while leaving an isolated point labeled as noise. */
TEST(Dbscan, BlobPlusOutlier) {
    const std::vector<Point> points{{0, 0, 0}, {0.1F, 0, 0}, {0, 0.1F, 0}, {20, 20, 20}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, -1}));
}

/**
 * @test Adopt previously visited noise as a border point without expanding through it to an
 * outlier.
 */
TEST(Dbscan, PreviouslyNoisyBorderPointDoesNotExpand) {
    // At x=0 there are only 3 neighbours, so it is initially noise.
    // x=0.9 is core and adopts x=0. The point x=-0.9 stays noise:
    // it is close to that border point but to no core point.
    const std::vector<Point> points{{0, 0, 0}, {0.9F, 0, 0}, {1.1F, 0, 0},
                                    {1.2F, 0, 0}, {-0.9F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 4), {1, 1, 1, 1, -1}));
}

/**
 * @test Assign an unvisited border point to its core cluster without expanding through that
 * border.
 */
TEST(Dbscan, UnvisitedBorderPointDoesNotExpand) {
    // The core is visited first this time, exercising the other border path.
    const std::vector<Point> points{{0.9F, 0, 0}, {1.1F, 0, 0}, {1.2F, 0, 0},
                                    {0, 0, 0}, {-0.9F, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 4), {1, 1, 1, 1, -1}));
}

/**
 * @test Follow a chain of core points beyond the initial radius and include both border endpoints.
 */
TEST(Dbscan, ExpansionReachesBeyondInitialNeighbourhood) {
    std::vector<Point> points;
    for (int x = 0; x < 20; ++x) {
        points.push_back({static_cast<float>(x), 0, 0});
    }
    // Each interior point is core; both endpoints are border points.
    // Reaching the far endpoint requires appending new seeds repeatedly.
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.01F, 3), std::vector<int>(20, 1)));
}

/**
 * @test Recover the known memberships of three sphere surfaces and sparse noise from a fixed-seed
 * scene.
 */
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

/** @test Recover a cluster and an outlier when all coordinates are negative. */
TEST(Dbscan, NegativeCoordinates) {
    const std::vector<Point> points{{-10, -5, -3}, {-10.1F, -5, -3}, {-10, -5.1F, -3},
                                    {-30, -30, -30}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.3F, 3), {1, 1, 1, -1}));
}

/** @test Include points exactly eps apart and separate them when the radius is slightly smaller. */
TEST(Dbscan, IncludesExactEpsBoundary) {
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{0, 0, 0}, {3, 4, 0}}, 5.0F, 2), {1, 1}));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{0, 0, 0}, {3, 4, 0}}, 4.9F, 2), {-1, -1}));
}

/** @test Count coincident samples individually when deciding whether a point is core. */
TEST(Dbscan, DuplicatePointsCountSeparately) {
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan({{1, 2, 3}, {1, 2, 3}, {1, 2, 3}}, 0.1F, 3),
                                 {1, 1, 1}));
}

/**
 * @test Assign consecutive cluster IDs in input discovery order, independent of spatial position.
 */
TEST(Dbscan, ClusterNumbersFollowDiscoveryOrder) {
    // Exact labels are intentional here: numbering itself is the contract tested.
    EXPECT_EQ(dbscan::dbscan({{20, 0, 0}, {-20, 0, 0}, {0, 0, 0}}, 1.0F, 1),
              (std::vector<int>{1, 2, 3}));
}

/**
 * @test Reject nonpositive or nonfinite eps and minSamples below one before handling empty input.
 */
TEST(Dbscan, RejectsInvalidParametersEvenForEmptyInput) {
    for (const float eps : {0.0F, -1.0F, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()}) {
        EXPECT_THROW(dbscan::dbscan({}, eps, 1), std::invalid_argument);
    }
    EXPECT_THROW(dbscan::dbscan({}, 1.0F, 0), std::invalid_argument);
    EXPECT_THROW(dbscan::dbscan({}, 1.0F, -1), std::invalid_argument);
}

/** @test Check that the public naive reference entry point can cluster a single point. */
TEST(Dbscan, NamedReferenceIsAvailable) {
    EXPECT_EQ(dbscan::dbscanNaive({{0, 0, 0}}, 1.0F, 1), (std::vector<int>{1}));
}

/**
 * @test Compare every grid neighborhood with a full scan, including duplicates, radius boundaries,
 * and output reuse.
 */
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

/**
 * @test Keep cells distinct at both packed-coordinate limits on every axis and match naive
 * clustering.
 */
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

/** @test Reject out-of-range cell coordinates on each axis instead of wrapping packed keys. */
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

/**
 * @test Reject invalid cell sizes and nonfinite coordinates when constructing the grid directly.
 */
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

/**
 * @test Compare grid and full-scan memberships on shuffled scenes over fixed seeds, radii, and
 * density thresholds.
 */
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

/**
 * @test Cluster 200 coincident points together and match the full-scan reference on a dense
 * neighborhood.
 */
TEST(Dbscan, DenseCloudMatchesNaive) {
    const std::vector<Point> points(200, Point{-0.1F, 0.1F, -0.1F});
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.5F, 4),
                                  dbscan::dbscanNaive(points, 0.5F, 4)));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.5F, 4), std::vector<int>(200, 1)));
}

/**
 * @test Keep points outside the radius separate at extreme scales where float squared distances
 * would fail.
 */
TEST(Dbscan, SquaredDistancesDoNotOverflowOrUnderflowFloat) {
    for (const float eps : {1e-30F, 1e30F}) {
        const std::vector<Point> points{{0, 0, 0}, {1.5F * eps, 0, 0}};
        EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, eps, 2), {-1, -1}));
        EXPECT_TRUE(equivalentLabels(dbscan::dbscanNaive(points, eps, 2), {-1, -1}));
    }
}

/**
 * @test Use double distance arithmetic to reject a pair that float subtraction could round onto
 * the radius.
 */
TEST(Dbscan, FloatRoundingDoesNotConnectPointsOutsideRadius) {
    const std::vector<Point> points{{-1e-8F, 0, 0}, {1, 0, 0}};
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 1.0F, 2), {-1, -1}));
    EXPECT_TRUE(equivalentLabels(dbscan::dbscanNaive(points, 1.0F, 2), {-1, -1}));
}

/** @test Require both clustering entry points to reject infinite and NaN coordinates. */
TEST(Dbscan, BothEntryPointsRejectNonFiniteCoordinates) {
    for (const float value : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
        const std::vector<Point> points{{0, 0, value}};
        EXPECT_THROW(dbscan::dbscan(points, 1.0F, 1), std::invalid_argument);
        EXPECT_THROW(dbscan::dbscanNaive(points, 1.0F, 1), std::invalid_argument);
    }
}

/**
 * @test Match the double distance predicate near zero across axes, signs, query orders, and radius
 * scales.
 */
TEST(Grid, NearZeroRadiusBoundaryMatchesDistancePredicate) {
    // Rounding accepts a pair two cells apart, but must still reject a point
    // one float step beyond the radius. Check all axes, signs and query orders.
    for (const float eps : {0.125F, 1.0F, 1024.0F}) {
        for (const float sign : {-1.0F, 1.0F}) {
            for (std::size_t axis = 0; axis < 3; ++axis) {
                SCOPED_TRACE(::testing::Message() << "eps=" << eps
                             << " sign=" << sign << " axis=" << axis);
                std::array<float, 3> a{0, 0, 0}, b{0, 0, 0}, c{0, 0, 0};
                a[axis] = -sign * eps * 1e-20F;
                b[axis] = sign * eps;
                c[axis] = sign * std::nextafter(eps, std::numeric_limits<float>::infinity());
                const std::vector<Point> points{
                    {a[0], a[1], a[2]}, {b[0], b[1], b[2]}, {c[0], c[1], c[2]}};
                const dbscan::Grid grid(points, eps);
                const double radius2 = static_cast<double>(eps) * eps;
                ASSERT_LE(dbscan::dist2(points[0], points[1]), radius2);
                ASSERT_GT(dbscan::dist2(points[0], points[2]), radius2);
                for (std::size_t query = 0; query < points.size(); ++query) {
                    std::vector<std::uint32_t> actual, expected;
                    grid.neighbours(query, actual);
                    std::sort(actual.begin(), actual.end());
                    for (std::uint32_t i = 0; i < points.size(); ++i) {
                        if (dbscan::dist2(points[query], points[i]) <= radius2) expected.push_back(i);
                    }
                    EXPECT_EQ(actual, expected) << "query=" << query;
                }
                EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, eps, 2),
                                            dbscan::dbscanNaive(points, eps, 2)));
            }
        }
    }
}

/**
 * @test Compare grid and naive memberships across 40 fixed seeds, six scales, duplicates, and four
 * density thresholds.
 */
TEST(Dbscan, RandomizedMembershipAcrossCoordinateScales) {
    for (unsigned seed = 0; seed < 40; ++seed) {
        std::mt19937 random(seed);
        std::uniform_real_distribution<float> coordinate(-3.0F, 3.0F);
        for (const float scale : {1e-12F, 0.125F, 0.5F, 2.0F, 128.0F, 1e12F}) {
            std::vector<Point> points;
            for (int i = 0; i < 128; ++i) {
                points.push_back({scale * coordinate(random), scale * coordinate(random),
                                  scale * coordinate(random)});
            }
            // Exercise duplicate samples in addition to general random positions.
            points.push_back(points.front());
            for (const int minimum : {1, 2, 4, 200}) {
                SCOPED_TRACE(::testing::Message() << "seed=" << seed << " scale=" << scale
                                                  << " minimum=" << minimum);
                EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, scale, minimum),
                                              dbscan::dbscanNaive(points, scale, minimum)));
            }
        }
    }
}

/** @test Preserve memberships after an exactly representable translation of a fixed-seed cloud. */
TEST(Dbscan, TranslationPreservesMembershipAwayFromRoundingBoundaries) {
    std::mt19937 random(91);
    std::uniform_int_distribution<int> coordinate(-16, 16);
    std::vector<Point> points;
    for (int i = 0; i < 200; ++i) {
        points.push_back({0.125F * coordinate(random), 0.125F * coordinate(random),
                          0.125F * coordinate(random)});
    }
    const auto expected = dbscan::dbscan(points, 0.75F, 4);
    for (auto& point : points) {
        point.x += 128; point.y -= 256; point.z += 32;
    }
    EXPECT_TRUE(equivalentLabels(dbscan::dbscan(points, 0.75F, 4), expected));
}

/**
 * @test Assign a shared border to the first discovered component without merging the two core
 * components.
 */
TEST(Dbscan, AmbiguousBorderDoesNotMergeCoreComponents) {
    std::vector<Point> points{{-1.5F,0,0}, {-1.6F,0,0}, {-1.7F,0,0}, {-0.9F,0,0},
                              {0,0,0}, {0.9F,0,0}, {1.5F,0,0}, {1.6F,0,0}, {1.7F,0,0}};
    for (int order = 0; order < 2; ++order) {
        const auto labels = dbscan::dbscan(points, 1.0F, 4);
        // The central border follows the first component visited, in either order.
        EXPECT_TRUE(equivalentLabels(labels, {1,1,1,1,1,2,2,2,2}));
        EXPECT_TRUE(equivalentLabels(labels, dbscan::dbscanNaive(points, 1.0F, 4)));
        std::reverse(points.begin(), points.end());
    }
}

}  // namespace
