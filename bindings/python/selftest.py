#!/usr/bin/env python3
"""Self-test for the Python binding.

Run from a checkout or build tree, for example:

    python3 bindings/python/selftest.py --library build/libmandelbrot.so

Exits with code 3 when numpy is missing so ctest can mark it as skipped.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

try:
    import numpy as np
except ImportError:
    print("numpy is required for the self-test", file=sys.stderr)
    sys.exit(3)

import mandelbrot


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", help="path to the mandelbrot shared library")
    args = parser.parse_args()
    if args.library:
        mandelbrot.load_library(args.library)

    print("library version:", mandelbrot.version())

    with mandelbrot.Mandelbrot(64, 32, max_iterations=64, threads=2, kernel="scalar") as ctx:
        ctx.set_view(1.0, -0.5, 0.0)
        colors, iterations = ctx.render(colors=True, iterations=True)
        check(colors.shape == (32, 64, 3) and colors.dtype == np.uint8, "color shape/dtype")
        check(iterations.shape == (32, 64) and iterations.dtype == np.int32, "iteration shape/dtype")
        check(iterations[16, 32] == 64, "the view center must be inside the set")
        check(ctx.kernel_name == "scalar", "forced kernel name")
        check(ctx.storage_width >= 64, "padded storage width")

        padded_colors = np.zeros((32, ctx.storage_width, 3), dtype=np.uint8)
        padded_iterations = np.zeros((32, ctx.storage_width), dtype=np.int32)
        ctx.render_into(padded_colors, padded_iterations)
        check(np.array_equal(padded_colors[:, :64], colors), "render_into colors")
        check(np.array_equal(padded_iterations[:, :64], iterations), "render_into iterations")

        ctx.set_pixel_order("rgb")
        rgb = ctx.render()
        check(np.array_equal(rgb[..., 0], colors[..., 2]), "rgb red channel")
        check(np.array_equal(rgb[..., 2], colors[..., 0]), "rgb blue channel")
        ctx.set_pixel_order("bgr")

        with tempfile.TemporaryDirectory() as directory:
            bmp = Path(directory) / "frame.bmp"
            ppm = Path(directory) / "frame.ppm"
            ctx.write_image(bmp)
            ctx.write_image(ppm, format="pnm")
            check(bmp.read_bytes()[:2] == b"BM", "bmp magic")
            check(ppm.read_bytes()[:2] == b"P6", "pnm magic")

        one_shot = mandelbrot.render_frame(64, 32, max_iterations=64, threads=2, zoom=1.0,
                                           center=(-0.5, 0.0), kernel="scalar")
        check(np.array_equal(one_shot, colors), "one-shot API matches the context API")

    try:
        mandelbrot.Mandelbrot(0, 0, 0)
    except mandelbrot.MandelbrotError as error:
        check("width" in str(error), "error message mentions the invalid argument")
    else:
        raise AssertionError("invalid dimensions must raise MandelbrotError")

    print("python binding self-test OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
