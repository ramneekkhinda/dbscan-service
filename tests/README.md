# Test guide

Each C++ test has a Doxygen-style `@test` comment above it describing the
behavior it checks. Python test functions and helpers have docstrings. Function
contracts are documented where functions are declared in headers, or above
definitions for internal helpers such as `clusterPoints()`.

## Run the tests

From the repository root, with a C++17 compiler, CMake, and Python 3 installed:

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The first configuration fetches GoogleTest and needs network access. Python 3
enables the integration test, which is registered only when Python is found.

To list tests or run a subset:

```sh
ctest --test-dir build -N
ctest --test-dir build -R '^(Dbscan|Grid)\.' --output-on-failure
ctest --test-dir build -R '^integration$' --output-on-failure
```

## Unit tests

| File / suites | What is checked |
| --- | --- |
| `unit/test_dbscan.cpp`: `LabelComparison`, `Point` | The membership-comparison helper and squared-distance calculation |
| `unit/test_dbscan.cpp`: `Dbscan` | Known cluster memberships, noise and border behavior, expansion, discovery order, input validation, duplicates, and numerical edge cases |
| `unit/test_dbscan.cpp`: `Grid` | Neighbor sets against a full scan, output reuse, packed-key limits, negative coordinates, and floating-point radius boundaries |
| `unit/test_protocol.cpp`: `Protocol` | Exact header bytes, request/response validation, socket transfers, point/label payloads, and numeric limits |
| `unit/test_protocol.cpp`: `Metrics` | Concurrent updates, exact counters, active connections, and cumulative histogram buckets |
| `unit/test_xyz_io.cpp`: `XyzTest` | Float text round trips, empty input, comments, extra columns, malformed line reporting, and read-time point limits |

### Why retain the naive implementation?

The server calls `dbscan()`, which searches using a grid. Tests also call
`dbscanNaive()`, which checks every point during each neighbor query. Comparing
their results helps detect errors introduced by narrowing the search to grid
cells. Direct grid tests compare the actual neighbor indices, not just clusters.

Both entry points reuse `clusterPoints()`. Its `Search` template parameter lets
each entry point supply a lambda with the same `(index, output)` interface. The
naive lambda captures the points and squared radius; the grid lambda captures
the grid object. This keeps expansion logic shared and allows compiler inlining.

Agreement between these entry points does not independently verify their shared
expansion logic. Tests with known expected labels check that behavior separately.
`equivalentLabels()` allows positive cluster IDs to be renumbered, but rejects
changed memberships, splits, merges, noise changes, and unfinished zero labels.
Tests specifically about discovery order compare the numeric labels directly.

## Integration test

`integration/integration.py` uses only the Python standard library. CTest passes
the built server, client, and scene-generator paths to the script. The script
starts real servers on local ports and uses temporary directories for output.
It terminates child servers and cleans up files even when checks fail.

| Function | What is checked |
| --- | --- |
| `run_end_to_end_checks()` | Generated XYZ input passes through the real client and server into output files matching known memberships; also checks invalid and truncated requests, concurrent clients, empty/bad input, output reuse, metrics HTTP behavior, and extreme radii |
| `fake_response_checks()` | The real client rejects six malformed or error replies from small fake servers and leaves no output files |
| `run_stress_checks()` | All 44 truncated-request byte boundaries, three fragmentation sizes, 240 requests with 16 workers, 24 abrupt resets, metrics during traffic, repeated traffic-scene ground truth, and open descriptors returning near baseline |

Integration checks exercise the server's grid implementation against known
labels, rather than comparing it with the naive implementation. They check
networking, validation, file output, and metrics together with clustering.

### Timing measurements

The end-to-end checks measure ten 5,000-point requests. The stress checks schedule
50 traffic-scene requests at a target 10 Hz and report latency statistics and
requests exceeding 100 ms. These measurements do not enforce a speed threshold
or guarantee throughput. CTest's overall integration timeout is 150 seconds.
`benchmark_dbscan` separately compares naive and grid clustering time without TCP.

## Documentation style

Use `/** @brief ... */` for C++ function summaries and `@param`, `@return`,
`@throws`, or `@tparam` where a contract needs explanation. Test comments use
`@test` to describe the scenario and expected behavior; explain the failure they
guard against when it is not obvious from the name. Keep Python explanations in
docstrings. These are source comments; no generated Doxygen site is configured.
