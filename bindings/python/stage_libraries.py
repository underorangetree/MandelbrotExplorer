#!/usr/bin/env python3
"""Stages the built shared libraries into the Python package directory.

Wheels bundle the native libraries as package data; pip does not build them.
Run this after a normal CMake build, then build/install the wheel:

    cmake --build build --config Release
    python bindings/python/stage_libraries.py --build-dir build [--video]
    python -m build --wheel bindings/python --outdir dist

The video library is optional (it depends on OpenCV) and is only staged when
``--video`` is given. ``--clean`` removes previously staged libraries instead.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

PACKAGE_DIR = Path(__file__).resolve().parent / "mandelbrot_explorer"
REPO_ROOT = Path(__file__).resolve().parents[2]

_CORE_STEM = "mandelbrot_explorer"
_VIDEO_STEM = "mandelbrot_explorer_video"


def _is_library(name: str, stem: str) -> bool:
    if not (name.startswith(stem) or name.startswith("lib" + stem)):
        return False
    if stem == _CORE_STEM and "_video" in name:
        return False
    return name.endswith((".dll", ".dylib")) or ".so" in name


def _find(build_dir: Path, stem: str, config: str | None) -> list[Path]:
    found: list[Path] = []
    for path in build_dir.rglob("*"):
        if not path.is_file():
            continue
        if config is not None and config not in path.parts:
            continue
        if _is_library(path.name, stem):
            found.append(path)
    return found


def _newest_by_name(paths: list[Path]) -> dict[str, Path]:
    newest: dict[str, Path] = {}
    for path in paths:
        current = newest.get(path.name)
        if current is None or path.stat().st_mtime > current.stat().st_mtime:
            newest[path.name] = path
    return newest


def _clean() -> int:
    removed = 0
    for library in PACKAGE_DIR.iterdir():
        if not library.is_file():
            continue
        if _is_library(library.name, _CORE_STEM) or _is_library(library.name, _VIDEO_STEM):
            library.unlink()
            print("removed", library.name)
            removed += 1
    if removed == 0:
        print("nothing to clean")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build",
                        help="build directory to search (default: build)")
    parser.add_argument("--config", help="only look inside this configuration directory (e.g. Release)")
    parser.add_argument("--video", action="store_true",
                        help="also stage the optional OpenCV-based video library")
    parser.add_argument("--clean", action="store_true",
                        help="remove previously staged libraries instead of copying")
    args = parser.parse_args()

    if args.clean:
        return _clean()

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = (REPO_ROOT / build_dir).resolve()
    if not build_dir.is_dir():
        print(f"build directory not found: {build_dir}", file=sys.stderr)
        return 1

    if not args.video and _find(build_dir, _VIDEO_STEM, args.config):
        print("note: the video library was built but is not staged (pass --video)")

    stems = [_CORE_STEM] + ([_VIDEO_STEM] if args.video else [])
    staged = 0
    for stem in stems:
        for name, source in sorted(_newest_by_name(_find(build_dir, stem, args.config)).items()):
            shutil.copy2(source, PACKAGE_DIR / name)
            print(f"staged {name} <- {source}")
            staged += 1
    if staged == 0:
        print(f"no libraries found under {build_dir}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
