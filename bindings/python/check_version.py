#!/usr/bin/env python3
"""Checks that a release tag matches the package and project versions.

    python bindings/python/check_version.py v0.6.0

Fails unless the tag, ``__version__`` in mandelbrot_explorer/__init__.py and
the CMake ``project(... VERSION ...)`` all agree. Run this before publishing;
the wheel workflow uses it for ``v*`` tags so a release cannot ship a
mismatched version.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def package_version() -> str:
    text = (REPO_ROOT / "bindings/python/mandelbrot_explorer/__init__.py").read_text(encoding="utf-8")
    match = re.search(r'^__version__ = "([^"]+)"', text, re.MULTILINE)
    if match is None:
        raise SystemExit("could not find __version__ in bindings/python/mandelbrot_explorer/__init__.py")
    return match.group(1)


def project_version() -> str:
    text = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(\s*\S+\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)", text)
    if match is None:
        raise SystemExit("could not find project(... VERSION ...) in CMakeLists.txt")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag", help="release tag, e.g. v0.6.0")
    args = parser.parse_args()

    version = package_version()
    project = project_version()
    expected = f"v{version}"
    if args.tag != expected or version != project:
        print(f"tag {args.tag!r}, package version {version!r}, CMake project version {project!r}",
              file=sys.stderr)
        print(f"expected tag {expected!r} with matching CMake project version", file=sys.stderr)
        return 1
    print(f"version check ok: {args.tag} == {version} == CMake {project}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
