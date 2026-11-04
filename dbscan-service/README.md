# DBSCAN service

A C++17 lidar clustering library, TCP service, command-line client and Prometheus
exporter. DBSCAN is implemented here using a uniform grid, with a naive reference
implementation for testing. Runtime dependencies: the C++ standard library and
Linux/POSIX sockets. GoogleTest is fetched only for tests.

## Build and test on Linux

Requires CMake >= 3.16, g++ or clang++, Git and internet access on the first
configuration to fetch GoogleTest v1.14.0. GoogleTest is a test-only dependency;
the library uses only the C++ standard library.

From this directory:

Stop an already-running server with Ctrl+C before rebuilding its binary. Start it
again afterward to use the new code; a running process does not reload changes.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For CMake 3.16, which predates `ctest --test-dir`, use instead:

```sh
(cd build && ctest --output-on-failure)
```

Release is the default for single-configuration generators. Project targets
compile with `-Wall -Wextra -Wpedantic`. To build the binaries without fetching
GoogleTest, configure with `-DBUILD_TESTING=OFF`.

The `dbscan` library contains only clustering; `service_io` contains protocol,
file I/O and metrics. The server, client and tests link the clustering library.
Python 3 is optional for the automated end-to-end test (standard library only).
When it is available, CTest also checks concurrent clients, invalid and truncated
messages, ground-truth output files, and the HTTP endpoint. The suite
includes GoogleTest cases and a Python integration test. Every test has a timeout;
timing measurements are informational rather than flaky performance assertions.

The local review passed all 37 CTest checks in both a clean Release build with
warnings treated as errors and a Debug AddressSanitizer/UndefinedBehaviorSanitizer
build. Both the 1,000-point sphere scene and the 5,000-point traffic scene also
matched sklearn and their generated ground truth after the numeric fixes. These
checks exercise the supported paths; they do not establish production readiness.

## Run the complete service

In terminal 1, from this directory:

```sh
./build/dbscan_server --port 5555 --metrics-port 9091
```

In terminal 2:

```sh
./build/processPointcloud --input data/sample_scene.xyz --output-dir out_demo \
    --eps 0.65 --min_samples 4 127.0.0.1:5555
curl http://127.0.0.1:9091/metrics
```

`out_demo` must be new or empty. The client creates it and writes `noise.xyz`,
`cluster_1.xyz`, etc. The supplied scene yields three 300-point clusters and 100
noise points. An empty scene produces an empty `noise.xyz`. To repeat a run, choose
a different empty output directory. Ctrl+C stops the server; in-flight requests
are not drained. Both listeners bind all IPv4 interfaces.

The only input needed by the client is the XYZ file. `.labels` and `.objects.txt`
sidecars are test ground truth/descriptions, not inputs to the clustering service.

The service handles one request per connection in a detached thread. Each handler
owns its request data and descriptor. Socket reads and writes retry EINTR and
handle partial transfers; writes suppress SIGPIPE. Accepted service connections
and the client use 30-second send/receive timeouts. These are per blocking I/O
operation, not an overall request deadline. The metrics listener handles scrapes
serially with a five-second socket timeout and an 8 KiB header limit. There is no
authentication, TLS, connection pool or admission control; this is a small
take-home service. See the deployment limits below before using it outside a
controlled environment.

## Files and reference comparison

XYZ files contain three whitespace-separated finite floats per line, in metres.
Blank lines and comments beginning with `#` after optional whitespace are skipped;
extra columns are ignored. Malformed input reports its line number. The client
rejects a cloud as soon as it exceeds one million points, while reading. Output uses
nine significant digits, enough to round-trip float32 values.

Regenerate the supplied scene and one-label-per-point ground truth (fixed seed):

```sh
./build/make_scene data/sample_scene.xyz
```

### Synthetic traffic scene

```sh
python3 tools/make_road_scene.py
./build/processPointcloud --input data/road_scene.xyz --output-dir out_road_demo \
    --eps 0.6 --min_samples 4 127.0.0.1:5555
```

This generates a fixed-seed, 5,000-point scene: three cars (900 points each), a van
(1,000), two pedestrians (250 each), a pole (150), a barrier (350), and isolated
noise (300). Vehicles have box-shaped bodies/cabins and cylindrical wheels. These
are stylised surface samples, not a physical lidar scan: there is no occlusion,
range-dependent sampling or sensor model. Coordinates are in metres; z is up.

`road_scene.labels` contains expected object membership, and `road_scene.objects.txt`
maps IDs to object descriptions. At eps=0.6 and min_samples=4, clusters 1-3 are cars,
4 is the van, 5-6 are pedestrians, 7 is the pole and 8 is the barrier. This mapping
describes this generated scene; DBSCAN itself groups by density and does not
recognise vehicle or pedestrian classes. Ground-truth and sklearn checks passed
for all 5,000 points.

The input assumes ground has already been removed. `road_scene_lanes.xyz` provides
separate road-edge and lane-divider points for visual context. Open it alongside
the input or output clouds in CloudCompare, but do not add it to the clustering
input: dense ground/lane points could bridge objects. Hide noise to inspect the
vehicles and colour the eight output clusters separately. New runs must use a new
or empty output directory.

Optional reference check, with the server running (Python dependencies are for
testing only):

```sh
python3 -m venv build/reference-env
build/reference-env/bin/pip install scikit-learn
build/reference-env/bin/python tools/compare_sklearn.py data/sample_scene.xyz \
    --eps 0.65 --min_samples 4 --ground-truth data/sample_scene.labels
```

The script compares cluster membership up to renumbering, with exact noise
agreement, using [sklearn DBSCAN](https://scikit-learn.org/stable/modules/generated/sklearn.cluster.DBSCAN.html).
Local validation passed against sklearn for the supplied scene and six additional
fixed-seed scene/parameter combinations; the supplied scene also matched its ground truth.
Floating-point radius boundaries and ambiguous border points can differ between
implementations. For visual inspection, open the XYZ files in CloudCompare or
MeshLab, assigning x/y/z to the first three columns. Load each cluster separately
and give it a distinct colour; inspect `noise.xyz` as a separate cloud. These
viewers are optional and are not used to implement DBSCAN.

## Wire protocol

All fields and payloads are little-endian with IEEE-754 float32 values. The current
implementation explicitly requires a little-endian host. Fields are copied
individually at fixed offsets; C++ structs are never sent directly. Point payloads
are flattened into triples and label payloads are converted to int32.

| Request offset | Type | Field |
| --- | --- | --- |
| 0 | uint32 | magic = 0x44425343 |
| 4 | uint16 | version = 1 |
| 6 | uint16 | reserved = 0 |
| 8 | float32 | eps |
| 12 | uint32 | min_samples |
| 16 | uint32 | n_points, at most 1,000,000 |
| 20 | float32 triples | x, y, z for each point |

| Response offset | Type | Field |
| --- | --- | --- |
| 0 | uint32 | magic = 0x44425343 |
| 4 | uint16 | version = 1 |
| 6 | uint16 | status |
| 8 | uint32 | n_points |
| 12 | float32 | dbscan_ms, includes grid construction |
| 16 | int32 array | labels in input order, only on success |

Statuses: 0 OK, 1 bad magic, 2 bad version, 3 bad parameters/coordinates,
4 too many points, 5 internal error. Error responses have n_points=0 and no label
payload. Truncated requests close the connection. Eps must be finite and positive;
min_samples must fit positive int. Non-finite or out-of-grid-range coordinates are
rejected with status 3. If a failure occurs after a response header starts, the
connection closes instead of appending a second header to the label stream.

## Metrics

`GET /metrics` defaults to port 9091 and returns the
[Prometheus text exposition format](https://prometheus.io/docs/instrumenting/exposition_formats/).
One mutex protects updates and rendering, so histogram counts and sums form a
consistent snapshot.

| Metric | Meaning |
| --- | --- |
| dbscan_requests_total | Completed connection handlers, including failed requests |
| dbscan_errors_total | Rejected, truncated, internal or transport failures |
| dbscan_points_total | Points in completed clustering computations |
| dbscan_active_connections | Current clustering handlers; excludes metric scrapes |
| dbscan_duration_seconds | Histogram of completed clustering computations |

The histogram has cumulative `le` buckets at 1, 2.5, 5, 10, 25, 50, 100 and 1000 ms,
plus `+Inf`, `_sum` and `_count`. Computation statistics remain counted if sending
the result later fails. Example Prometheus scrape configuration:

```yaml
scrape_configs:
  - job_name: dbscan
    static_configs:
      - targets: ['localhost:9091']
```

## Library contract

Include `dbscan/dbscan.h` and link the CMake target `dbscan`.

```cpp
std::vector<dbscan::Point> points{{0, 0, 0}, {0.1F, 0, 0}, {10, 10, 10}};
auto labels = dbscan::dbscan(points, 0.2F, 2); // {1, 1, -1}
```

Labels retain input order. Noise is -1, and clusters are numbered from 1 in input
discovery order. Zero is an internal unvisited marker, never a final label.
Neighbourhoods include the query point and the eps boundary. A core point has at
least `minSamples` neighbours. Previously noisy points can become border points.
`eps` must be finite and positive and `minSamples` at least 1, including for empty
input; invalid parameters throw `std::invalid_argument`. Valid empty input returns
an empty vector. Both entry points reject non-finite coordinates. Input and wire
coordinates remain float32, but squared distances and eps squared use double
arithmetic to avoid float overflow/underflow. Values extremely close to a radius
or cell boundary remain subject to floating-point rounding; this is not exact
arithmetic. The grid also enforces the cell range described below.

`dbscan()` uses a uniform grid with cell size eps, checking 27 neighbouring cells
per query and filtering candidates by squared distance. It packs three signed
cell indices into a 63-bit key, using 21 bits per axis. Cell indices are computed
using `floor(coordinate / eps)`, including for negative coordinates. Supported
indices are -1,048,576 through 1,048,575 per axis; out-of-range input throws
`std::out_of_range` rather than wrapping into a different cell. Non-finite
coordinates throw `std::invalid_argument`. Points must outlive a directly created
`Grid` and must not be modified while it exists. Both DBSCAN entry points reject
point counts above `INT_MAX` with `std::length_error` because cluster labels are int.

`dbscanNaive()` remains the O(n^2) reference. Both implementations use O(n)
auxiliary memory. The grid usually reduces distance checks for spatially spread
clouds, but dense neighbourhoods can still require O(n^2) work.

Tests cover hand-labelled scenes, noise and border behaviour, negative coordinates,
boundary distances, duplicates, parameter validation, and three 300-point sphere
surfaces with 100 uniform noise points (fixed seed). Membership comparisons allow
renumbering while requiring identical noise assignments. Tests also compare the
grid with a full neighbour scan, exercise packed-key bounds, and compare grid and
naive clustering across 45 fixed-seed scene/parameter combinations and a dense
cloud.

## Benchmark

From this directory, after building:

```sh
./build/benchmark_dbscan
```

The benchmark generates 5,000 points (ten 400-point sphere surfaces plus 1,000
uniform noise points), uses eps=0.3 m and minSamples=6, warms up each implementation,
and prints its mean time over 20 calls. Grid timing includes rebuilding the index
on every call. Scene generation is excluded. Times describe this synthetic scene
on your machine; they do not establish performance for every cloud or service
workload. Cluster and noise counts are printed as a quick sanity check;
the unit tests provide the membership comparisons.

For the reviewed WSL Release build (GCC 13.3), the benchmark measured 4.626 ms
per grid call and 72.207 ms per naive call. The automated 5,000-point TCP workload
measured 6.198 ms median round-trip time (7.082 ms maximum over ten calls), with
4.296 ms median server clustering time. These are measurements, not timing assertions or
guarantees for arbitrary clouds. For profiling, build with
`-DCMAKE_BUILD_TYPE=RelWithDebInfo` and run `perf record -g ./build/benchmark_dbscan`
followed by `perf report` on a Linux system with perf enabled. Dense neighbourhoods,
distance filtering, hash lookups and allocation are the main areas to examine.

For a warnings-as-errors review build, configure with `-DCMAKE_CXX_FLAGS=-Werror`.
For a separate memory/undefined-behaviour check with GCC on Linux/WSL:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined -no-pie"
cmake --build build-sanitize -j
(cd build-sanitize && ctest --output-on-failure)
```

## Deployment scope and remaining production work

This project implements the requested take-home service and can be demonstrated
on a trusted Linux/WSL machine. Passing its tests does not certify it for an
untrusted public deployment. In particular:

- Each accepted connection starts a detached thread with no concurrency or total
  memory admission limit. The point limit is per request, not a process-wide budget.
- Dense clouds can cause quadratic clustering work. Socket timeouts do not bound
  CPU time or a client's total elapsed request time. DNS lookup and connect use
  operating-system timeouts rather than a configured overall client deadline.
- Both listeners bind all interfaces and provide no authentication or TLS. Use
  network access controls; encryption/authentication would need a deployment layer.
- Shutdown terminates in-flight work. The metrics listener handles scrapes serially;
  one slow connection can delay others. There is no supervisor/restart policy.
- Output files are written individually, without transactional directory publishing.
  A disk/write failure may leave partial output. Retry into a fresh empty directory.

A production deployment needs requirements for connection/memory budgets, compute
deadlines or cancellation, graceful draining, network security and sustained-load
tests on representative real lidar. Those changes would expand the deliberately
small thread-per-connection exercise design. Current validation covers synthetic
scenes, protocol errors, partial I/O, concurrent clients, output membership and
metrics consistency, plus numeric-range regressions. It is not a real-scene
accuracy or prolonged-load qualification.

## Repository contents

`include/` and `src/` contain the C++ implementation; `tests/` contains automated
checks; `tools/` contains scene generators, a benchmark and optional sklearn
comparison; `data/` contains reproducible sample input and ground truth. The
Prometheus exporter is built in; running a separate Prometheus installation is not
required. No monitoring binary or dashboard setup is included.

`.gitignore` excludes build folders, generated `out*` directories, Python caches,
local virtual environments/editor files, and personal study material in
`LEARNING.md`, `learning/`, `notes/`, and `docs/learning/`. Public documentation,
source comments, sample data and ground truth remain part of the repository.
