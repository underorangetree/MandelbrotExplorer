#!/usr/bin/env python3
"""Renders one frame and writes it to a file.

Usage:
    python3 render_image.py [--library PATH] [--size WxH] [--maxiter N]
                            [--zoom Z] [--center X Y] [--kernel NAME]
                            [--iterations] output.bmp
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import mandelbrot


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="output image (bmp, ppm or pnm)")
    parser.add_argument("--library", help="path to the mandelbrot shared library")
    parser.add_argument("--size", default="1280x720", help="frame size WxH (default 1280x720)")
    parser.add_argument("--maxiter", type=int, default=2000, help="maximum iterations")
    parser.add_argument("--zoom", type=float, default=1.0, help="zoom level")
    parser.add_argument("--center", nargs=2, type=float, metavar=("X", "Y"),
                        default=(-0.5, 0.0), help="view center (default -0.5 0)")
    parser.add_argument("--kernel", help="force a kernel (scalar|avx2|avx512|neon)")
    parser.add_argument("--iterations", action="store_true",
                        help="also render the iteration counts and print statistics")
    args = parser.parse_args()

    width, height = (int(value) for value in args.size.lower().split("x"))
    center = (args.center[0], args.center[1])
    format = "pnm" if args.output.endswith((".ppm", ".pnm")) else "bmp"
    if args.library:
        mandelbrot.load_library(args.library)

    if args.iterations:
        iterations = mandelbrot.render_frame(
            width, height, max_iterations=args.maxiter, zoom=args.zoom, center=center,
            kernel=args.kernel, colors=False, iterations=True)
        print(f"iterations: min {iterations.min()} max {iterations.max()} "
              f"mean {iterations.mean():.1f}")

    mandelbrot.render_image(args.output, width, height, max_iterations=args.maxiter,
                            zoom=args.zoom, center=center, kernel=args.kernel, format=format)
    print(f"wrote {args.output} ({width}x{height}, library {mandelbrot.version()})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
