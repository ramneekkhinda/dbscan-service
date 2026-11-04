# DBSCAN point-cloud service

A C++17 point-cloud clustering library and TCP service with a separate
`processPointcloud` client and Prometheus-compatible metrics exporter.

The implementation, tests, synthetic data and tools are in
[`dbscan-service/`](dbscan-service/). See the
[project README](dbscan-service/README.md) for the wire protocol, file format,
metrics, sample runs, profiling commands and deployment limits.

Build and test on Linux or Ubuntu WSL:

```sh
cd dbscan-service
cmake -S . -B build
cmake --build build -j
(cd build && ctest --output-on-failure)
```

Requires CMake 3.16 or newer and a C++17 compiler. Test configuration fetches
GoogleTest; Python 3 enables the integration test. The runtime uses only the C++
standard library and POSIX sockets. This is a take-home implementation for trusted
environments; the project README records the remaining production deployment work.
