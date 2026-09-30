#!/usr/bin/env bash
# Builds the Linux wheel inside a manylinux container.
#
# The wheel is platform specific and bundles the core library; building inside
# the manylinux image keeps the glibc requirement low, so the wheel installs on
# much older distributions than one built on a recent host.
#
# Run it through docker; the repository root must be mounted at /work and the
# UID mapping keeps the generated files owned by the caller:
#
#   docker run --rm -v "$PWD:/work" -w /work -e HOME=/tmp \
#       -u "$(id -u):$(id -g)" quay.io/pypa/manylinux_2_28_x86_64 \
#       bash /work/bindings/python/manylinux_build.sh dist
#
# The wheel is written to <outdir> (default: dist) inside the repository.
set -euo pipefail

OUTDIR=${1:-dist}
cd /work

# Newest /opt/python interpreter that ships pip (the distro python3 has none).
PYTHON=""
for candidate in $(ls -d /opt/python/cp3[0-9][0-9]-cp3[0-9][0-9]/bin/python3 2>/dev/null | sort -V); do
    if "$candidate" -m pip --version >/dev/null 2>&1; then
        PYTHON="$candidate"
    fi
done
if [ -z "$PYTHON" ]; then
    echo "no /opt/python interpreter with pip found" >&2
    exit 1
fi
echo "using $("$PYTHON" --version) at $PYTHON"

"$PYTHON" -m pip install --quiet --user cmake build wheel auditwheel patchelf
export PATH="$HOME/.local/bin:$PATH"

cmake -S . -B build-manylinux -DMANDELBROT_EXPLORER_WITH_VIDEO=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-manylinux -j"$(nproc)" --target mandelbrot_explorer

"$PYTHON" bindings/python/stage_libraries.py --build-dir build-manylinux
"$PYTHON" -m build --wheel bindings/python --outdir build-manylinux/dist
"$PYTHON" bindings/python/retag_wheel.py build-manylinux/dist

auditwheel repair build-manylinux/dist/*.whl -w build-manylinux/repaired
mkdir -p "$OUTDIR"
cp build-manylinux/repaired/*.whl "$OUTDIR/"
"$PYTHON" bindings/python/stage_libraries.py --clean

echo "manylinux wheel(s):"
ls "$OUTDIR"/*manylinux*.whl
