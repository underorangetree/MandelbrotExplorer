#!/usr/bin/env python3
"""Self-test for the Python binding.

Run from a checkout or build tree, for example:

    python3 bindings/python/selftest.py --library build/libmandelbrot_explorer.so

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

import mandelbrot_explorer as me


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", help="path to the mandelbrot shared library")
    args = parser.parse_args()
    if args.library:
        me.load_library(args.library)

    print("library version:", me.version())
    check(me.__version__ == me.version(),
          f"package version {me.__version__} does not match the library version {me.version()}")

    with me.Mandelbrot(64, 32, max_iterations=64, threads=2, kernel="scalar") as ctx:
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

        # Parameter errors surface as Python exceptions.
        try:
            ctx.set_kernel("bogus")
        except me.MandelbrotError:
            pass
        else:
            raise AssertionError("an unknown kernel must raise MandelbrotError")

        try:
            ctx.set_pixel_order("bogus")
        except ValueError:
            pass
        else:
            raise AssertionError("an unknown pixel order must raise ValueError")

        try:
            ctx.render(colors=False, iterations=False)
        except ValueError:
            pass
        else:
            raise AssertionError("rendering without outputs must raise ValueError")

        with tempfile.TemporaryDirectory() as directory:
            bmp = Path(directory) / "frame.bmp"
            ppm = Path(directory) / "frame.ppm"
            ctx.write_image(bmp)
            ctx.write_image(ppm, format="pnm")
            check(bmp.read_bytes()[:2] == b"BM", "bmp magic")
            check(ppm.read_bytes()[:2] == b"P6", "pnm magic")

        one_shot = me.render_frame(64, 32, max_iterations=64, threads=2, zoom=1.0,
                                           center=(-0.5, 0.0), kernel="scalar")
        check(np.array_equal(one_shot, colors), "one-shot API matches the context API")

    # Video export uses the optional mandelbrot_explorer_video library.
    with tempfile.TemporaryDirectory() as directory:
        clip = Path(directory) / "clip.mp4"
        still = Path(directory) / "still.mp4"
        try:
            me.write_video(clip, 160, 120, max_iterations=32, threads=2,
                                   fps=5.0, duration=1.0, kernel="scalar",
                                   center=(-0.5, 0.0), start_zoom=1.0, end_zoom=2.0)
            me.write_video(still, 64, 64, max_iterations=16, threads=1,
                                   fps=1.0, duration=1.0, start_zoom=1.5, end_zoom=1.5)
        except FileNotFoundError as error:
            print("mandelbrot_explorer_video not available:", error)
            return 3
        except me.VideoError as error:
            print("no usable video backend:", error)
            return 3
        check(clip.exists() and clip.stat().st_size > 0, "video file was written")
        check(still.exists() and still.stat().st_size > 0, "static view video was written")

    try:
        me.Mandelbrot(0, 0, 0)
    except me.MandelbrotError as error:
        check("width" in str(error), "error message mentions the invalid argument")
    else:
        raise AssertionError("invalid dimensions must raise MandelbrotError")

    print("python binding self-test OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
