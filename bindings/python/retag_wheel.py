#!/usr/bin/env python3
"""Retags built wheels with the host platform.

setuptools marks a pure-Python wheel as ``py3-none-any`` even when the package
bundles native libraries as package data; installed or uploaded as-is, such a
wheel would claim to work on every platform. This script rewrites the tag
(filename and metadata) to the platform reported by the host:

    python -m wheel tags --remove --python-tag py3 --abi-tag none ...

On macOS the tag is derived from the host architecture and macOS version
instead of ``sysconfig.get_platform()``: a universal2 Python reports
``macosx-*-universal2`` even though the bundled library only supports one
architecture, and clang defaults its deployment target to the host version.

Run it after ``pip wheel``/``python -m build`` and before installing or
uploading the wheel.
"""

from __future__ import annotations

import argparse
import glob
import platform
import subprocess
import sys
import sysconfig
from pathlib import Path


def platform_tag() -> str:
    if sys.platform == "darwin":
        version = platform.mac_ver()[0].split(".")[0] or "11"
        return f"macosx_{version}_0_{platform.machine()}"
    return sysconfig.get_platform().replace("-", "_").replace(".", "_")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("outdir", nargs="?", default="dist",
                        help="directory containing the wheels (default: dist)")
    parser.add_argument("--platform-tag",
                        help="override the platform tag instead of deriving it from the host")
    args = parser.parse_args()

    wheels = sorted(glob.glob(str(Path(args.outdir) / "*.whl")))
    if not wheels:
        print(f"no wheels found in {args.outdir}", file=sys.stderr)
        return 1

    tag = args.platform_tag or platform_tag()
    subprocess.check_call([sys.executable, "-m", "wheel", "tags", "--remove",
                           "--python-tag", "py3", "--abi-tag", "none",
                           "--platform-tag", tag] + wheels)
    for wheel in sorted(glob.glob(str(Path(args.outdir) / "*.whl"))):
        print("retagged:", wheel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
