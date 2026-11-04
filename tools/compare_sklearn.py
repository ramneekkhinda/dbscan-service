#!/usr/bin/env python3
"""Testing only: compare server labels with sklearn and optional ground truth."""
import argparse
import socket
import struct

import numpy as np
from sklearn.cluster import DBSCAN


def read_exact(connection, count):
    chunks = bytearray()
    while len(chunks) < count:
        part = connection.recv(count - len(chunks))
        if not part:
            raise RuntimeError("truncated response")
        chunks.extend(part)
    return chunks


def equivalent(actual, expected):
    if len(actual) != len(expected):
        return False
    forward, reverse = {}, {}
    for a, b in zip(actual, expected):
        a, b = int(a), int(b)
        if a == -1 or b == -1:
            if a != b:
                return False
        elif a <= 0 or b <= 0 or forward.setdefault(a, b) != b or reverse.setdefault(b, a) != a:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input")
    parser.add_argument("--eps", type=float, required=True)
    parser.add_argument("--min_samples", type=int, required=True)
    parser.add_argument("--server", default="127.0.0.1:5555")
    parser.add_argument("--ground-truth")
    args = parser.parse_args()
    points = np.loadtxt(args.input, dtype="<f4", usecols=(0, 1, 2), ndmin=2)
    eps = float(np.float32(args.eps))  # Use exactly the epsilon sent over the wire.
    if len(points) > 1000000 or not np.isfinite(points).all():
        parser.error("invalid point count or coordinates")
    host, port = args.server.rsplit(":", 1)
    with socket.create_connection((host, int(port)), timeout=30) as connection:
        connection.sendall(struct.pack("<IHHfII", 0x44425343, 1, 0, eps,
                                       args.min_samples, len(points)) + points.tobytes())
        magic, version, status, count, milliseconds = struct.unpack("<IHHIf", read_exact(connection, 16))
        if magic != 0x44425343 or version != 1 or status != 0 or count != len(points):
            raise RuntimeError(f"bad response: status={status}, count={count}")
        actual = np.frombuffer(read_exact(connection, 4 * count), dtype="<i4")
    # Both implementations widen the same float32 coordinates for distance math.
    # Algorithm-specific radius-boundary rounding can still differ.
    reference = DBSCAN(eps=eps, min_samples=args.min_samples).fit_predict(
        points.astype(np.float64)) if len(points) else np.array([])
    expected = np.where(reference == -1, -1, reference + 1)
    if not equivalent(actual, expected):
        raise SystemExit("FAIL: sklearn membership differs (check boundary/ambiguous border points)")
    if args.ground_truth and not equivalent(actual, np.loadtxt(args.ground_truth, dtype=int, ndmin=1)):
        raise SystemExit("FAIL: ground-truth membership differs")
    print(f"PASS: {len(points)} points match sklearn"
          f"{' and ground truth' if args.ground_truth else ''}; server DBSCAN {milliseconds:.3f} ms")


if __name__ == "__main__":
    main()
