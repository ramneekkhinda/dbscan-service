#include "xyz_io.h"
#include <gtest/gtest.h>
#include <unistd.h>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

/** @brief Give each XYZ test its own temporary file and remove it afterward. */
class XyzTest : public ::testing::Test {
protected:
    /** @brief Create a unique empty file and store its path for the test. */
    void SetUp() override {
        char pattern[] = "/tmp/dbscan-xyz-XXXXXX";
        const int fd = mkstemp(pattern);
        ASSERT_GE(fd, 0);
        close(fd);
        path = pattern;
    }
    /** @brief Remove the temporary file after each test. */
    void TearDown() override { std::filesystem::remove(path); }
    std::filesystem::path path;
};
/**
 * @test Preserve float coordinates through text output and read an overwritten empty file as no
 * points.
 */
TEST_F(XyzTest, FloatRoundTripAndEmptyFile) {
    const std::vector<dbscan::Point> points{{-1.23456789F, 0, 12345.678F}, {0.00001F, -2, 3}};
    dbscan::writeXyz(path, points);
    const auto actual = dbscan::readXyz(path);
    ASSERT_EQ(actual.size(), points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        EXPECT_EQ(actual[i].x, points[i].x);
        EXPECT_EQ(actual[i].y, points[i].y);
        EXPECT_EQ(actual[i].z, points[i].z);
    }
    dbscan::writeXyz(path, {});
    EXPECT_TRUE(dbscan::readXyz(path).empty());
}
/** @test Ignore blank lines and comments and accept whitespace-separated columns after XYZ. */
TEST_F(XyzTest, SkipsBlankLinesCommentsAndExtraColumns) {
    { std::ofstream file(path); file << "\n # comment\n \t\r\n1 2 3 ignored extras\n"; }
    const auto points = dbscan::readXyz(path);
    ASSERT_EQ(points.size(), 1U);
    EXPECT_EQ(points[0].z, 3);
}
/**
 * @test Reject incomplete, malformed, nonfinite, or overflowing coordinates and report the
 * physical line number.
 */
TEST_F(XyzTest, ReportsMalformedLineNumber) {
    for (const auto* bad : {"1 2", "a 2 3", "1 2 3oops", "1 2 nan", "1e50 2 3"}) {
        { std::ofstream file(path); file << "# heading\n1 2 3\n" << bad << '\n'; }
        try {
            dbscan::readXyz(path);
            FAIL() << "accepted " << bad;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string(error.what()).find("line 3"), std::string::npos);
        }
    }
}

/**
 * @test Enforce the point limit while reading, including exact-limit acceptance and zero-limit
 * empty input.
 */
TEST_F(XyzTest, PointLimitIsEnforcedWhileReading) {
    { std::ofstream file(path); file << "1 2 3\n# ignored\n4 5 6\n7 8 9\n"; }
    EXPECT_THROW(dbscan::readXyz(path, 2), std::length_error);
    EXPECT_EQ(dbscan::readXyz(path, 3).size(), 3U);
    EXPECT_THROW(dbscan::readXyz(path, 0), std::length_error);
    { std::ofstream file(path); }
    EXPECT_TRUE(dbscan::readXyz(path, 0).empty());
}
