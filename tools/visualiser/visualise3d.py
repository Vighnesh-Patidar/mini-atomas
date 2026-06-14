#!/usr/bin/env python3
"""
3D matplotlib visualiser for the MithAtomas flocking demo (3D variant).

Reads JSON-line snapshots from stdin (one per tick), each of shape:
    {"tick": <int>, "robots": [{"id": "...", "x": ..., "y": ..., "z": ...}, ...]}

Usage:
    ./build/examples/flocking_demo_3d/flocking_demo_3d \
        | python3 tools/visualiser/visualise3d.py [--save PATH.gif] [--fps N]

When --save is given, the run is exported as an animated GIF and the
window is not opened. Otherwise an interactive window is shown.

Requires: matplotlib. For GIF export: pillow (matplotlib's default
PillowWriter dependency).
"""

from __future__ import annotations

import argparse
import json
import sys

try:
    import matplotlib.pyplot as plt
    import matplotlib.animation as animation
    from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 — register projection
except ImportError:
    print("Needs matplotlib. Install with: pip install matplotlib pillow",
          file=sys.stderr)
    sys.exit(1)


def read_frames(stream):
    frames = []
    for line in stream:
        line = line.strip()
        if not line:
            continue
        try:
            frames.append(json.loads(line))
        except json.JSONDecodeError as e:
            print(f"skipping malformed line: {e}", file=sys.stderr)
    return frames


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--save", help="Export animation to this GIF path instead of showing.")
    ap.add_argument("--fps", type=int, default=20, help="Output frames per second.")
    args = ap.parse_args()

    frames = read_frames(sys.stdin)
    if not frames:
        print("No frames received on stdin.", file=sys.stderr)
        return 1

    all_x = [r["x"] for f in frames for r in f["robots"]]
    all_y = [r["y"] for f in frames for r in f["robots"]]
    all_z = [r["z"] for f in frames for r in f["robots"]]
    pad = 3.0
    xlo, xhi = min(all_x) - pad, max(all_x) + pad
    ylo, yhi = min(all_y) - pad, max(all_y) + pad
    zlo, zhi = min(all_z) - pad, max(all_z) + pad

    fig = plt.figure(figsize=(5, 5), dpi=80)
    ax = fig.add_subplot(111, projection="3d")
    ax.set_xlim(xlo, xhi)
    ax.set_ylim(ylo, yhi)
    ax.set_zlim(zlo, zhi)
    ax.set_xlabel("x (m)")
    ax.set_ylabel("y (m)")
    ax.set_zlabel("z (m)")
    ax.set_title("MithAtomas — 20-robot 3D flocking demo")

    # Initial scatter; we update its data each frame.
    initial = frames[0]["robots"]
    xs = [r["x"] for r in initial]
    ys = [r["y"] for r in initial]
    zs = [r["z"] for r in initial]
    scatter = ax.scatter(xs, ys, zs, s=40, c="#1f77b4", depthshade=True)
    tick_text = ax.text2D(0.02, 0.95, "", transform=ax.transAxes)

    def update(i):
        f = frames[i]
        xs = [r["x"] for r in f["robots"]]
        ys = [r["y"] for r in f["robots"]]
        zs = [r["z"] for r in f["robots"]]
        scatter._offsets3d = (xs, ys, zs)
        tick_text.set_text(f"tick = {f['tick']}")
        # Slow camera orbit for visual depth cue.
        ax.view_init(elev=22, azim=(i * 0.6) % 360)
        return scatter, tick_text

    anim = animation.FuncAnimation(
        fig, update, frames=len(frames),
        interval=1000.0 / args.fps, blit=False, repeat=False,
    )

    if args.save:
        writer = animation.PillowWriter(fps=args.fps)
        anim.save(args.save, writer=writer)
        print(f"saved {args.save}", file=sys.stderr)
    else:
        plt.show()

    return 0


if __name__ == "__main__":
    sys.exit(main())
