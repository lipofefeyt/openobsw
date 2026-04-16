#!/usr/bin/env bash
# OpenOBSW Workspace Setup
# Usage: source scripts/setup-workspace.sh

# ------------------------------------------------------------------ #
# Cross-compilation toolchain (aarch64 ZynqMP)                       #
# ------------------------------------------------------------------ #
AARCH64_GCC=$(find /nix/store -name "aarch64-unknown-linux-gnu-gcc" -path "*/gcc-wrapper*/bin/*" -type f 2>/dev/null | head -1)
if [ -z "$AARCH64_GCC" ]; then
    echo "[toolchain] Installing aarch64 cross-compiler..."
    nix-env -iA nixpkgs.pkgsCross.aarch64-multiplatform.stdenv.cc > /dev/null 2>&1 && echo "[toolchain] aarch64-linux-gnu-gcc installed"
fi
# Add nix profile to PATH
if [ -d "$HOME/.nix-profile/bin" ]; then
    export PATH="$HOME/.nix-profile/bin:$PATH"
fi
# Verify
if command -v aarch64-unknown-linux-gnu-gcc &>/dev/null; then
    echo "[toolchain] aarch64: $(aarch64-unknown-linux-gnu-gcc --version | head -1)"
else
    echo "[toolchain] WARNING: aarch64-unknown-linux-gnu-gcc not found"
fi

# set -e intentionally omitted — sourced scripts must not exit the parent shell
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
pip install -q pydantic pyyaml pytest
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

# General alisases
alias bashed='vim ~/.bashrc'
alias bashed='source ~/.bashrc'

# Build aliases
alias omkbuild='cmake --build $OPENOBSW_REPO/build -j$(nproc) 2>&1 | tail -5'
alias omkctest='cd $OPENOBSW_REPO/build && ctest --output-on-failure && cd $OPENOBSW_REPO'
alias omkclean='rm -rf $OPENOBSW_REPO/build && echo "Build cleaned"'
alias omkrebuild='omkclean && cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build -DCMAKE_BUILD_TYPE=Debug -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON -G "Unix Makefiles" -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null && omkbuild'
alias omkbuild-aarch64='cmake -S $REPO -B $REPO/build_aarch64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$REPO/cmake/aarch64-linux-gnu.cmake -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=ON -G "Unix Makefiles" -DPython3_EXECUTABLE=$REPO/.venv/bin/python3 > /dev/null && cmake --build $REPO/build_aarch64 -j$(nproc) 2>&1 | tail -3'
alias omksim-aarch64='qemu-aarch64 -L $(find /nix/store -name "ld-linux-aarch64.so.1" -path "*/glibc-aarch64*" 2>/dev/null | head -1 | xargs dirname | xargs dirname) $REPO/build_aarch64/sim/obsw_sim'
alias omkfile-aarch64='file $REPO/build_aarch64/sim/obsw_sim'
alias omkbuild-zynqmp='cmake -S $REPO -B $REPO/build_zynqmp -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=$REPO/cmake/aarch64-linux-gnu.cmake -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=OFF -DOBSW_BUILD_ZYNQMP=ON -G "Unix Makefiles" -DPython3_EXECUTABLE=$REPO/.venv/bin/python3 > /dev/null && cmake --build $REPO/build_zynqmp -j$(nproc) 2>&1 | tail -5'
alias omkfile-zynqmp='file $REPO/build_zynqmp/obsw_zynqmp'
alias omksim='$OPENOBSW_REPO/build/sim/obsw_sim'
alias omktest-sim='python3 $OPENOBSW_REPO/sim/send_ping.py'

# Probably to be changed in the future
alias baremetal-build='cmake --build build_zynqmp_baremetal -j$(nproc)'
alias baremetal-objcopy='/tmp/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-objcopy -O binary build_zynqmp_baremetal/obsw_zynqmp build_zynqmp_baremetal/obsw_zynqmp.bin'

echo ""
echo "=== Setup complete ==="
echo ""
echo "Available commands:"
echo "  omkbuild    — build openobsw"
echo "  omkctest    — run all C tests"
echo "  omkclean    — clean build"
echo "  omkrebuild  — clean + configure + build"
echo "  omksim      — run obsw_sim"
