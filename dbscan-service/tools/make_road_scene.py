#!/usr/bin/env python3
"""Generate stylised, ground-filtered traffic objects (not a physical lidar simulation)."""
import argparse
import math
from pathlib import Path
import random


def box(randomizer, count, centre, size):
    """Uniform surface samples, choosing faces proportional to their areas."""
    areas = [size[1]*size[2], size[0]*size[2], size[0]*size[1]]
    points = []
    for _ in range(count):
        axis = randomizer.choices(range(3), weights=areas)[0]
        point = [randomizer.uniform(-length/2, length/2) for length in size]
        point[axis] = randomizer.choice((-1, 1)) * size[axis]/2
        points.append(tuple(a+b for a, b in zip(point, centre)))
    return points


def car(randomizer, van=False):
    if van:
        points = box(randomizer, 880, (0, 0, 1.55), (5.4, 2.1, 2.3))
    else:
        points = box(randomizer, 500, (0, 0, 0.675), (4.4, 1.8, 0.65))
        points += box(randomizer, 280, (0, 0, 1.35), (2.1, 1.5, 0.7))
    # Four wheels: cylinders whose axes run across the vehicle (along y).
    for axle in (-1.4, 1.4):
        for side in (-1, 1):
            for _ in range(30):
                angle = randomizer.uniform(0, 2*math.pi)
                points.append((axle + 0.33*math.cos(angle),
                               side*(1.06 if van else 0.94) + randomizer.uniform(-0.05, 0.05),
                               0.33 + 0.33*math.sin(angle)))
    return points


def person(randomizer):
    points = []
    for _ in range(140):
        angle = randomizer.uniform(0, 2*math.pi)
        points.append((0.22*math.cos(angle), 0.14*math.sin(angle), randomizer.uniform(0.6, 1.5)))
    for _ in range(70):
        z, angle = randomizer.uniform(-1, 1), randomizer.uniform(0, 2*math.pi)
        ring = math.sqrt(1-z*z)
        points.append((0.17*ring*math.cos(angle), 0.17*ring*math.sin(angle), 1.72+0.17*z))
    for leg in (-0.12, 0.12):
        for _ in range(20):
            angle = randomizer.uniform(0, 2*math.pi)
            points.append((leg+0.07*math.cos(angle), 0.07*math.sin(angle), randomizer.uniform(0.1, 0.7)))
    return points


def write_xyz(path, points):
    path.write_text("".join(" ".join(f"{value:.9g}" for value in point) + "\n" for point in points))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path,
                        default=Path(__file__).resolve().parents[1] / "data/road_scene.xyz")
    args = parser.parse_args()
    randomizer = random.Random(42)
    points, labels, descriptions = [], [], []

    def add(name, cloud, x=0, y=0, heading=0):
        label = len(descriptions) + 1
        cosine, sine = math.cos(heading), math.sin(heading)
        points.extend((x + cosine*a - sine*b, y + sine*a + cosine*b, z) for a, b, z in cloud)
        labels.extend([label]*len(cloud))
        descriptions.append(f"{label}: {name} ({len(cloud)} points)")

    add("car in near lane", car(randomizer), -12, -3)
    add("car in near lane", car(randomizer), 0, -3)
    add("opposing car", car(randomizer), 11, 3, math.pi)
    add("opposing van", car(randomizer, van=True), -10, 3, math.pi)
    add("pedestrian", person(randomizer), 4, 7)
    add("pedestrian", person(randomizer), 7, 7)
    pole = []
    for _ in range(150):
        angle = randomizer.uniform(0, 2*math.pi)
        pole.append((0.1*math.cos(angle), 0.1*math.sin(angle), randomizer.uniform(0, 3.5)))
    add("roadside pole", pole, -3, 8)
    add("roadside barrier", box(randomizer, 350, (0, 0, 0.65), (6, 0.4, 1.1)), 14, -7)

    # Isolate synthetic noise from every object and from other noise samples.
    # This makes the sidecar labels unambiguous at eps=0.6, min_samples=4.
    while len(points) < 5000:
        candidate = (randomizer.uniform(-22, 22), randomizer.uniform(-11, 11),
                     randomizer.uniform(0.2, 4))
        if all(sum((a-b)**2 for a, b in zip(candidate, point)) > 0.9**2 for point in points):
            points.append(candidate)
            labels.append(-1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_xyz(args.output, points)
    args.output.with_suffix(".labels").write_text("".join(f"{label}\n" for label in labels))
    args.output.with_suffix(".objects.txt").write_text("\n".join(descriptions) + "\n-1: noise (300 points)\n")
    # Separate visual context: solid road edges and a dashed lane divider at z=0.
    markings = [(step/10, y, 0) for step in range(-220, 221) for y in (-6.5, 0, 6.5)
                if y != 0 or math.floor((step/10+22)/3) % 2 == 0]
    write_xyz(args.output.with_name(args.output.stem + "_lanes.xyz"), markings)
    print(f"Wrote {len(points)} points to {args.output}; use eps=0.6, min_samples=4")
    print("\n".join(descriptions))
    print("Lane markings are a separate visual context cloud, excluded from clustering.")


if __name__ == "__main__":
    main()
