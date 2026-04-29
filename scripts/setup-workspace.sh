#!/usr/bin/env bash
# scripts/setup-workspace.sh
#
# Run ONCE to install dependencies, configure and build all targets.
# Do NOT source this every terminal — use scripts/activate.sh instead.
#
# Usage:
#   bash scripts/setup-workspace.sh
#   source scripts/activate.sh   # then use aliases

set -e

REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
cd "$REPO"

echo "=== OpenOBSW Workspace Setup ==="

# ------------------------------------------------------------------ #
# 1. Python venv                                                      #
# ------------------------------------------------------------------ #
echo "[1/5] Python venv..."

NIX_PY=$(find /nix/store -name "python3" -path "*/bin/python3" 2>/dev/null | \
    xargs -I{} sh -c '{} -c "import yaml" 2>/dev/null && echo {}' | head -1)
VENV_PY="${NIX_PY:-$(which python3)}"

if [ ! -f "$REPO/.venv/bin/activate" ]; then
    echo "    Creating venv from $VENV_PY..."
    "$VENV_PY" -m venv "$REPO/.venv"
fi
source "$REPO/.venv/bin/activate"
pip install -q pydantic pyyaml pytest
pip install -q -e "$REPO/srdb/"
echo "    Python: $(python3 --version)"

# ------------------------------------------------------------------ #
# 2. aarch64 cross-compiler                                          #
# ------------------------------------------------------------------ #
echo "[2/5] aarch64 toolchain..."

if ! command -v aarch64-unknown-linux-gnu-gcc &>/dev/null; then
    echo "    Installing aarch64 cross-compiler (nix)..."
    nix-env -iA nixpkgs.pkgsCross.aarch64-multiplatform.stdenv.cc > /dev/null 2>&1
fi
[ -d "$HOME/.nix-profile/bin" ] && export PATH="$HOME/.nix-profile/bin:$PATH"

if command -v aarch64-unknown-linux-gnu-gcc &>/dev/null; then
    echo "    aarch64: $(aarch64-unknown-linux-gnu-gcc --version | head -1)"
else
    echo "    WARNING: aarch64-unknown-linux-gnu-gcc not found"
fi

# ------------------------------------------------------------------ #
# 3. Host sim build (x86_64 + unit tests)                            #
# ------------------------------------------------------------------ #
echo "[3/5] Host sim build..."
PYTHON_EXE="$REPO/.venv/bin/python3"

cmake -S "$REPO" -B "$REPO/build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON \
    -DOBSW_BUILD_SIM=ON \
    -G "Unix Makefiles" \
    -DPython3_EXECUTABLE="$PYTHON_EXE" \
    > /dev/null 2>&1

cmake --build "$REPO/build" -j$(nproc) 2>&1 | grep -E "error:|Built target" | tail -5
echo "    obsw_sim: $(ls -lh $REPO/build/sim/obsw_sim 2>/dev/null | awk '{print $5}' || echo 'NOT FOUND')"

# ------------------------------------------------------------------ #
# 4. C unit tests                                                     #
# ------------------------------------------------------------------ #
echo "[4/5] C unit tests..."
cd "$REPO/build" && ctest --output-on-failure -q 2>&1 | tail -3
cd "$REPO"

# ------------------------------------------------------------------ #
# 5. Renode (optional)                                               #
# ------------------------------------------------------------------ #
echo "[5/5] Renode..."
if command -v renode &>/dev/null; then
    echo "    Renode: $(renode --version 2>&1 | head -1)"
else
    echo "    Renode not found — add 'pkgs.renode' to dev.nix to enable"
fi

echo ""
echo "=== Setup complete ==="
echo "Run: source scripts/activate.sh"