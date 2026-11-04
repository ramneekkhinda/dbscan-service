cmake_minimum_required(VERSION 3.16)
project(dbscan_service LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic)
find_package(Threads REQUIRED)

add_library(dbscan src/clustering/dbscan.cpp src/clustering/grid.cpp)
target_include_directories(dbscan PUBLIC include)

# Networking and file I/O are separate from the clustering-only library.
add_library(service_io src/service/protocol.cpp src/service/xyz_io.cpp src/service/metrics.cpp)
target_include_directories(service_io PUBLIC include src/service)
target_link_libraries(service_io PUBLIC Threads::Threads)
add_executable(dbscan_server apps/server/main.cpp)
target_link_libraries(dbscan_server PRIVATE dbscan service_io)
add_executable(processPointcloud apps/client/main.cpp)
target_link_libraries(processPointcloud PRIVATE dbscan service_io)
add_executable(make_scene tools/make_scene.cpp)
target_include_directories(make_scene PRIVATE fixtures)
target_link_libraries(make_scene PRIVATE dbscan service_io)

add_executable(benchmark_dbscan benchmarks/benchmark_dbscan.cpp)
target_include_directories(benchmark_dbscan PRIVATE fixtures)
target_link_libraries(benchmark_dbscan PRIVATE dbscan)

include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
        GIT_SHALLOW TRUE
    )
    set(BUILD_GMOCK OFF CACHE BOOL "Build GoogleMock" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_dbscan tests/unit/test_dbscan.cpp tests/unit/test_protocol.cpp tests/unit/test_xyz_io.cpp)
    target_include_directories(test_dbscan PRIVATE fixtures)
    target_link_libraries(test_dbscan PRIVATE dbscan service_io GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(test_dbscan PROPERTIES TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        add_test(NAME integration COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/integration.py
            $<TARGET_FILE:dbscan_server> $<TARGET_FILE:processPointcloud> $<TARGET_FILE:make_scene>)
        set_tests_properties(integration PROPERTIES TIMEOUT 60)
    endif()
endif()
