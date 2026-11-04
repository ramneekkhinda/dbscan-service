"""Standard-library-only end-to-end checks; CTest starts and stops its own server."""
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
import time

MAGIC = 0x44425343


def exact(connection, size):
    data = bytearray()
    while len(data) < size:
        part = connection.recv(size - len(data))
        if not part:
            raise RuntimeError("truncated response")
        data.extend(part)
    return data


def exchange(port, points=(), eps=0.65, minimum=4, magic=MAGIC, version=1,
             reserved=0, count=None, fragmented=False):
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
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    try:
        connection.request("GET", path)
        response = connection.getresponse()
        return response.status, response.read().decode()
    finally:
        connection.close()


def main(server_binary, client_binary, scene_binary):
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


if __name__ == "__main__":
    main(*sys.argv[1:])
