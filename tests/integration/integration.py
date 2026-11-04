"""Exercise real client/server processes using only the Python standard library.

CTest passes paths to the server, client, and scene-generator binaries. This
script starts isolated servers on available local ports, checks output against
known labels, then exercises transport failures, concurrency, and metrics. It
uses the server's grid implementation, not the naive reference from unit tests.
Temporary files and child servers are cleaned up on success and failure. Timing
statistics are diagnostic measurements, not performance pass/fail thresholds.
"""
import collections
import concurrent.futures
import http.client
import math
from pathlib import Path
import random
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time

MAGIC = 0x44425343


def exact(connection, size):
    """Receive exactly size bytes, or raise RuntimeError if the peer closes early."""
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise RuntimeError("truncated response")
        data.extend(part)
    return data


def exchange(port, points=(), eps=0.65, minimum=4, magic=MAGIC, version=1,
             reserved=0, count=None, fragmented=False):
    """Send one binary request and return (status, labels, clustering_ms).

    Header overrides deliberately construct invalid requests. A count override
    changes only the declared point count, not the payload. fragmented sends
    the header one byte at a time to exercise exact-length reads. Error replies
    must declare zero labels; valid replies must have the expected magic/version.
    """
    header = struct.pack("<IHHfII", magic, version, reserved, eps, minimum,
                         len(points) if count is None else count)
    payload = b"".join(struct.pack("<fff", *point) for point in points)
    with socket.create_connection(("127.0.0.1", port), timeout=10) as connection:
        if fragmented:
            for byte in header:
                connection.sendall(bytes([byte]))
        else:
            connection.sendall(header)
        if payload:
            connection.sendall(payload)
        reply = struct.unpack("<IHHIf", exact(connection, 16))
        assert reply[0:2] == (MAGIC, 1), reply
        labels = struct.unpack(f"<{reply[3]}i", exact(connection, reply[3] * 4)) if reply[2] == 0 else ()
        if reply[2] != 0:
            assert reply[3] == 0, reply
        return reply[2], labels, reply[4]


def scrape(port, path="/metrics"):
    """GET an HTTP path and return its status and decoded body, closing afterward."""
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        return response.status, response.read().decode()
    finally:
        connection.close()


def run_end_to_end_checks(server_binary, client_binary, scene_binary):
    """Check the full XYZ-to-TCP-to-cluster-files flow against known memberships.

    Also cover empty/single-point input, invalid headers and coordinates,
    truncated requests, concurrent clients, output-directory reuse, bad XYZ,
    metrics counters and HTTP errors, and extreme finite radii. Ten 5,000-point
    requests report TCP and server clustering timings without a speed threshold.
    The spawned server is stopped in finally, even when an assertion fails.
    """
    # Reserve distinct available ports, then release them immediately before startup.
    with socket.socket() as a, socket.socket() as b:
        a.bind(("127.0.0.1", 0)); b.bind(("127.0.0.1", 0))
        port, metrics_port = a.getsockname()[1], b.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="dbscan-integration-") as temporary:
        root = Path(temporary)
        with (root / "server.log").open("w+") as log:
            server = subprocess.Popen([server_binary, "--port", str(port), "--metrics-port",
                                       str(metrics_port)], stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 15
                while True:
                    try:
                        assert scrape(metrics_port)[0] == 200
                        break
                    except OSError:
                        if server.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("server did not start")
                        time.sleep(0.05)
                assert exchange(port)[:2] == (0, ())
                assert exchange(port, [(0, 0, 0)], minimum=1, fragmented=True)[:2] == (0, (1,))
                for options, status in [({"magic": 0}, 1), ({"version": 2}, 2),
                                        ({"minimum": 0}, 3), ({"eps": float("nan")}, 3),
                                        ({"reserved": 1}, 3), ({"count": 1000001}, 4),
                                        ({"points": [(float("nan"), 0, 0)]}, 3),
                                        ({"points": [(2000000, 0, 0)], "eps": 1}, 3)]:
                    assert exchange(port, **options)[0] == status, options
                for partial in [b"DBS", struct.pack("<IHHfII", MAGIC, 1, 0, 1, 1, 2) + b"short"]:
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                        connection.sendall(partial)
                        connection.shutdown(socket.SHUT_WR)
                        assert connection.recv(16) == b""
                scene = root / "scene.xyz"
                subprocess.run([scene_binary, str(scene)], check=True, capture_output=True)
                points = [tuple(map(float, line.split())) for line in scene.read_text().splitlines()]
                truth = list(map(int, scene.with_suffix(".labels").read_text().splitlines()))
                output = root / "out"
                command = [client_binary, "--input", str(scene), "--output-dir", str(output),
                           "--eps", "0.65", "--min_samples", "4", f"127.0.0.1:{port}"]
                result = subprocess.run(command, check=True, text=True, capture_output=True)
                assert len(list(output.glob("*.xyz"))) == 4
                for label in [-1, 1, 2, 3]:
                    name = "noise.xyz" if label == -1 else f"cluster_{label}.xyz"
                    actual = [tuple(map(float, line.split())) for line in (output / name).read_text().splitlines()]
                    assert collections.Counter(actual) == collections.Counter(
                        point for point, expected in zip(points, truth) if expected == label)
                # Refusing an existing output avoids leaving stale cluster files.
                assert subprocess.run(command, capture_output=True).returncode != 0
                def concurrent_request(_):
                    """Require one parallel grid-clustering response to match scene ground truth."""
                    status, labels, _ = exchange(port, points)
                    assert status == 0 and list(labels) == truth
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    list(pool.map(concurrent_request, range(16)))
                empty = root / "empty.xyz"
                empty.write_text("")
                empty_command = command.copy()
                empty_command[2] = str(empty); empty_command[4] = str(root / "empty-out")
                subprocess.run(empty_command, check=True, capture_output=True)
                assert (root / "empty-out" / "noise.xyz").read_text() == ""
                empty.write_text("1 2 invalid\n")
                assert subprocess.run(empty_command, capture_output=True).returncode != 0
                # A separate 5,000-point workload measures complete TCP round trips.
                randomizer = random.Random(42)
                large = []
                for sphere in range(10):
                    for _ in range(400):
                        z, angle = randomizer.uniform(-1, 1), randomizer.uniform(0, 2 * math.pi)
                        ring = math.sqrt(1 - z*z)
                        large.append((10*sphere + ring*math.cos(angle), ring*math.sin(angle), z))
                large.extend(tuple(randomizer.uniform(-100, 100) for _ in range(3)) for _ in range(1000))
                round_trip, clustering = [], []
                for _ in range(10):
                    start = time.perf_counter()
                    status, labels, milliseconds = exchange(port, large, eps=0.3, minimum=6)
                    round_trip.append((time.perf_counter() - start) * 1000)
                    clustering.append(milliseconds)
                    assert status == 0 and len(labels) == 5000
                deadline = time.monotonic() + 3
                while True:
                    status, metrics = scrape(metrics_port)
                    if "dbscan_active_connections 0\n" in metrics:
                        break
                    assert time.monotonic() < deadline
                    time.sleep(0.01)
                assert status == 200 and "# TYPE dbscan_duration_seconds histogram\n" in metrics
                values = dict(line.split() for line in metrics.splitlines() if line and not line.startswith("#"))
                assert int(values["dbscan_errors_total"]) >= 10
                assert int(values["dbscan_points_total"]) >= 67000
                assert values['dbscan_duration_seconds_bucket{le="+Inf"}'] == values["dbscan_duration_seconds_count"]
                assert scrape(metrics_port, "/missing")[0] == 404
                # Reach the exact header cap without completing a header. Do not
                # leave additional unread bytes that could cause a TCP reset on close.
                with socket.create_connection(("127.0.0.1", metrics_port), timeout=5) as connection:
                    connection.sendall(b"GET /metrics HTTP/1.1\r\nX: " + b"x" * (8192 - 26))
                    reply = connection.recv(1024)
                    assert reply.startswith(b"HTTP/1.1 431"), reply
                for eps in (1e-30, 1e30):
                    assert exchange(port, [(0, 0, 0), (1.5*eps, 0, 0)], eps=eps, minimum=2)[:2] == (0, (-1, -1))
                print(result.stdout.strip())
                print(f"5000-point TCP round trip: median {statistics.median(round_trip):.3f} ms, "
                      f"max {max(round_trip):.3f} ms; clustering median {statistics.median(clustering):.3f} ms")
                print("PASS: output ground truth, concurrent clients, invalid/truncated requests, metrics, empty input")
            except Exception:
                log.flush(); log.seek(0)
                print(log.read(), file=sys.stderr)
                raise
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill(); server.wait()


def fake_response_checks(client_binary, directory):
    """Require the real client to reject six malformed or unsuccessful replies.

    Fake servers send bad magic, a mismatched count, truncated labels, label zero,
    an error status, or a negative duration. Each client must exit unsuccessfully
    and leave its output directory absent or empty.
    """
    source = directory / "two.xyz"
    source.write_text("0 0 0\n0.1 0 0\n")
    replies = [struct.pack("<IHHIf", 0, 1, 0, 2, 0),
               struct.pack("<IHHIf", MAGIC, 1, 0, 5, 0),
               struct.pack("<IHHIfi", MAGIC, 1, 0, 2, 0, 1),
               struct.pack("<IHHIfii", MAGIC, 1, 0, 2, 0, 0, 1),
               struct.pack("<IHHIf", MAGIC, 1, 3, 0, 0),
               struct.pack("<IHHIf", MAGIC, 1, 0, 2, -1)]
    for index, reply in enumerate(replies):
        errors = []
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(5)
        port = listener.getsockname()[1]
        def serve():
            """Consume a complete client request, send this case's reply, and retain errors."""
            try:
                with listener, listener.accept()[0] as connection:
                    connection.settimeout(5)
                    header = struct.unpack("<IHHfII", exact(connection, 20))
                    exact(connection, header[-1] * 12)
                    connection.sendall(reply)
            except Exception as error:
                errors.append(error)
        thread = threading.Thread(target=serve)
        thread.start()
        output = directory / f"bad-response-{index}"
        try:
            result = subprocess.run([client_binary, "--input", str(source), "--output-dir", str(output),
                "--eps", "0.6", "--min_samples", "1", f"127.0.0.1:{port}"],
                capture_output=True, timeout=8)
            assert result.returncode != 0, f"client accepted malformed reply {index}"
            assert not output.exists() or not list(output.iterdir()), index
        finally:
            thread.join(timeout=6)
            assert not thread.is_alive() and not errors, errors


def run_stress_checks(server_binary, client_binary):
    """Exercise failure recovery and concurrent traffic against an isolated server.

    Cover 44 request EOF boundaries, three fragmentation sizes, 240 requests
    with 16 workers, 24 connection resets, and metrics scrapes during traffic.
    Then check 50 traffic-scene requests against ground truth at a target 10 Hz,
    allow open descriptors to return near baseline, and test malformed replies
    with fake servers. Latency is reported, not bounded by a performance assertion.
    """
    with socket.socket() as a, socket.socket() as b:
        a.bind(("127.0.0.1", 0)); b.bind(("127.0.0.1", 0))
        port, metrics_port = a.getsockname()[1], b.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="dbscan-extended-") as temporary:
        directory = Path(temporary)
        with (directory / "server.log").open("w+") as log:
            server = subprocess.Popen([server_binary, "--port", str(port), "--metrics-port", str(metrics_port)],
                                      stdout=log, stderr=log)
            stop = threading.Event()
            watcher = None
            try:
                deadline = time.monotonic() + 15
                while True:
                    try:
                        assert scrape(metrics_port)[0] == 200
                        break
                    except OSError:
                        if server.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("server startup failed")
                        time.sleep(0.05)
                baseline_fds = len(list(Path(f"/proc/{server.pid}/fd").iterdir()))
                header = struct.pack("<IHHfII", MAGIC, 1, 0, 0.6, 4, 2)
                # EOF at every header and point-payload byte boundary.
                for size in range(20):
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                        connection.sendall(header[:size]); connection.shutdown(socket.SHUT_WR)
                        assert connection.recv(16) == b"", size
                for size in range(24):
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                        connection.sendall(header + bytes(size)); connection.shutdown(socket.SHUT_WR)
                        assert connection.recv(16) == b"", size
                small = [(-2,0,0), (-1.9,0,0), (-1.8,0,0), (2,0,0), (2.1,0,0), (2.2,0,0), (20,0,0)]
                expected = (1,1,1,2,2,2,-1)
                payload = struct.pack("<IHHfII", MAGIC, 1, 0, 0.4, 3, len(small))
                payload += b"".join(struct.pack("<fff", *point) for point in small)
                for chunk_size in (1, 7, 13):
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                        for index in range(0, len(payload), chunk_size):
                            connection.sendall(payload[index:index+chunk_size])
                        reply = struct.unpack("<IHHIf", exact(connection, 16))
                        assert reply[:4] == (MAGIC, 1, 0, len(small)), reply
                        assert struct.unpack("<7i", exact(connection, 28)) == expected
                scrape_errors = []
                def watch():
                    """Scrape during traffic and check histogram/count consistency until stopped."""
                    while not stop.wait(0.02):
                        try:
                            status, text = scrape(metrics_port)
                            assert status == 200 and text.endswith("\n")
                            values = dict(line.split() for line in text.splitlines() if not line.startswith("#"))
                            count = int(values["dbscan_duration_seconds_count"])
                            assert int(values['dbscan_duration_seconds_bucket{le="+Inf"}']) == count
                            assert int(values["dbscan_errors_total"]) <= int(values["dbscan_requests_total"])
                        except Exception as error:
                            scrape_errors.append(str(error))
                watcher = threading.Thread(target=watch)
                watcher.start()
                def request(_):
                    """Require one concurrent request to return the known two-cluster labels."""
                    assert exchange(port, small, eps=0.4, minimum=3)[:2] == (0, expected)
                with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
                    list(pool.map(request, range(240)))
                # A reset during response processing must not kill the service.
                for _ in range(24):
                    with socket.create_connection(("127.0.0.1", port), timeout=5) as connection:
                        connection.sendall(payload)
                        connection.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                root = Path(__file__).resolve().parents[2]
                scene = root / "examples/data/traffic/road_scene.xyz"
                points = [tuple(map(float, line.split())) for line in scene.read_text().splitlines()]
                truth = tuple(map(int, scene.with_suffix(".labels").read_text().splitlines()))
                times, durations = [], []
                start = time.perf_counter()
                for iteration in range(50):
                    time.sleep(max(0, start + iteration * 0.1 - time.perf_counter()))
                    before = time.perf_counter()
                    status, labels, milliseconds = exchange(port, points, eps=0.6, minimum=4)
                    times.append((time.perf_counter() - before) * 1000)
                    durations.append(milliseconds)
                    assert status == 0 and labels == truth
                stop.set(); watcher.join(timeout=5)
                assert not watcher.is_alive() and not scrape_errors, scrape_errors
                deadline = time.monotonic() + 5
                while True:
                    current = len(list(Path(f"/proc/{server.pid}/fd").iterdir()))
                    if current <= baseline_fds + 1:
                        break
                    assert time.monotonic() < deadline, (baseline_fds, current)
                    time.sleep(0.02)
                fake_response_checks(client_binary, directory)
                assert exchange(port, small, eps=0.4, minimum=3)[:2] == (0, expected)
                p95 = sorted(times)[math.ceil(0.95 * len(times)) - 1]
                print("PASS: 44 EOF boundaries, fragmented payloads, 240 concurrent requests, "
                      "24 connection resets, concurrent metric scrapes, 6 malformed server replies")
                print(f"PASS: 50 x 5000-point requests scheduled at 10 Hz; full memberships match ground truth")
                print(f"TCP ms: median={statistics.median(times):.3f} p95={p95:.3f} max={max(times):.3f}; "
                      f"clustering median={statistics.median(durations):.3f}; "
                      f"requests over 100 ms={sum(value > 100 for value in times)}")
                print(f"FDs before={baseline_fds}, after={current}")
            except Exception:
                log.flush(); log.seek(0); print(log.read(), file=sys.stderr)
                raise
            finally:
                stop.set()
                if watcher is not None:
                    watcher.join(timeout=5)
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill(); server.wait()

def main(server_binary, client_binary, scene_binary):
    """Run the end-to-end and stress suites using the three CTest-supplied binaries."""
    run_end_to_end_checks(server_binary, client_binary, scene_binary)
    run_stress_checks(server_binary, client_binary)


if __name__ == "__main__":
    main(*sys.argv[1:])
