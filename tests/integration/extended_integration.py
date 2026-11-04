"""Additional local stress/failure tests. Uses temporary ports and output folders."""
import concurrent.futures
import math
from pathlib import Path
import runpy
import socket
import statistics
import struct
import subprocess
import sys
import tempfile
import threading
import time

helpers = runpy.run_path(str(Path(__file__).with_name("integration.py")))
exchange, exact, scrape = (helpers[name] for name in ("exchange", "exact", "scrape"))
MAGIC = 0x44425343


def fake_response_checks(client_binary, directory):
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


def main(server_binary, client_binary):
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


if __name__ == "__main__":
    main(*sys.argv[1:])
