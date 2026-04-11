#!/usr/bin/env bash
# OpenOBSW Workspace Setup
# Usage: source scripts/setup-workspace.sh

set -e
REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
cd "$REPO"

echo "=== OpenOBSW Workspace Setup ==="

# ------------------------------------------------------------------ #
# 1. Find Python with yaml support                                    #
# ------------------------------------------------------------------ #
echo "[1/4] Setting up Python venv..."
NIX_PY=$(find /nix/store -name "python3" -path "*/bin/python3" 2>/dev/null | \
    xargs -I{} sh -c '{} -c "import yaml" 2>/dev/null && echo {}' | head -1)

if [ -n "$NIX_PY" ]; then
    VENV_PY="$NIX_PY"
else
    VENV_PY=$(which python3)
fi

if [ ! -f "$REPO/.venv/bin/activate" ]; then
    echo "    Creating venv from $VENV_PY..."
    "$VENV_PY" -m venv "$REPO/.venv"
fi
source "$REPO/.venv/bin/activate"
pip install -q pydantic pyyaml
pip install -q -e "$REPO/srdb/"
echo "    Python: $(python3 --version)"
python3 -c "import yaml, pydantic; print('    deps: pyyaml + pydantic OK')"

# ------------------------------------------------------------------ #
# 2. Build                                                            #
# ------------------------------------------------------------------ #
echo "[2/4] Building openobsw..."
PYTHON_EXE="$REPO/.venv/bin/python3"

if [ ! -f "$REPO/build/Makefile" ]; then
    echo "    Configuring..."
    cmake -S "$REPO" -B "$REPO/build" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DOBSW_BUILD_TESTS=ON \
        -DOBSW_BUILD_SIM=ON \
        -G "Unix Makefiles" \
        -DPython3_EXECUTABLE="$PYTHON_EXE" \
        > /dev/null 2>&1
fi

echo "    Building..."
cmake --build "$REPO/build" -j$(nproc) 2>&1 | grep -E "error:|warning:|Built target|Error" | tail -10
echo "    obsw_sim: $(ls -lh $REPO/build/sim/obsw_sim 2>/dev/null | awk '{print $5}' || echo 'NOT FOUND')"

# ------------------------------------------------------------------ #
# 3. Run tests                                                        #
# ------------------------------------------------------------------ #
echo "[3/4] Running C tests..."
cd "$REPO/build" && ctest --output-on-failure -q 2>&1 | tail -5
cd "$REPO"

# ------------------------------------------------------------------ #
# 4. Aliases                                                          #
# ------------------------------------------------------------------ #
echo "[4/4] Setting up aliases..."

export OPENOBSW_REPO="$REPO"
export OPENOBSW_PYTHON="$PYTHON_EXE"

alias omkbuild='cmake --build $OPENOBSW_REPO/build -j$(nproc) 2>&1 | tail -5'
alias omkctest='cd $OPENOBSW_REPO/build && ctest --output-on-failure && cd $OPENOBSW_REPO'
alias omkclean='rm -rf $OPENOBSW_REPO/build && echo "Build cleaned"'
alias omkrebuild='omkclean && cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build -DCMAKE_BUILD_TYPE=Debug -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON -G "Unix Makefiles" -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null && omkbuild'
alias omksim='$OPENOBSW_REPO/build/sim/obsw_sim'
alias omktest-sim='python3 $OPENOBSW_REPO/sim/send_ping.py'

echo ""
echo "=== Setup complete ==="
echo ""
echo "Available commands:"
echo "  omkbuild    — build openobsw"
echo "  omkctest    — run all C tests"
echo "  omkclean    — clean build"
echo "  omkrebuild  — clean + configure + build"
echo "  omksim      — run obsw_sim"
