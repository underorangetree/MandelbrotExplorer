#!/usr/bin/env python3
"""Retags built wheels with the host platform.

setuptools marks a pure-Python wheel as ``py3-none-any`` even when the package
bundles native libraries as package data; installed or uploaded as-is, such a
wheel would claim to work on every platform. This script rewrites the tag
(filename and metadata) to the platform reported by ``sysconfig``:

    python -m wheel tags --remove --python-tag py3 --abi-tag none ...

Run it after ``pip wheel``/``python -m build`` and before installing or
uploading the wheel.
"""

from __future__ import annotations

import argparse
import glob
import subprocess
import sys
import sysconfig
from pathlib import Path


def platform_tag() -> str:
    return sysconfig.get_platform().replace("-", "_").replace(".", "_")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outdir", nargs="?", default="dist",
                        help="directory containing the wheels (default: dist)")
    args = parser.parse_args()

    wheels = sorted(glob.glob(str(Path(args.outdir) / "*.whl")))
    if not wheels:
        print(f"no wheels found in {args.outdir}", file=sys.stderr)
        return 1

    subprocess.check_call([sys.executable, "-m", "wheel", "tags", "--remove",
                           "--python-tag", "py3", "--abi-tag", "none",
                           "--platform-tag", platform_tag()] + wheels)
    for wheel in sorted(glob.glob(str(Path(args.outdir) / "*.whl"))):
        print("retagged:", wheel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
