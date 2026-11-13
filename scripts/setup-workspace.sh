#!/usr/bin/env bash
# scripts/setup-workspace.sh
#
# Install every tool openobsw needs on Ubuntu 24.04 (dev container or WSL2),
# create the Python venv, then build and test the host sim. Idempotent:
# each step skips work that is already done, so re-running is safe.
# Do NOT source this every terminal — use scripts/activate.sh instead.
#
# Usage:
#   bash scripts/setup-workspace.sh              # install + build + test
#   bash scripts/setup-workspace.sh --no-build   # install only
#   source scripts/activate.sh                   # then use aliases

set -euo pipefail

REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
cd "$REPO"
source "$REPO/scripts/env.sh"   # OPENOBSW_ENV, OPENOBSW_VENV
VENV="$OPENOBSW_VENV"

BUILD=1
for arg in "$@"; do
    case "$arg" in
        --no-build) BUILD=0 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

STEP="init"
trap 'echo "!! setup failed during: $STEP" >&2' ERR

step() { STEP="$1"; echo "[$1]"; }

# Download URL $1 to file $2.
fetch() { wget -q --show-progress "$1" -O "$2"; }

echo "=== openobsw workspace setup ==="

# ------------------------------------------------------------------ #
# 1. System packages                                                  #
# ------------------------------------------------------------------ #
step "1/6 system packages"
APT_PKGS=(
    cmake ninja-build gcc clang
    clang-format clang-tidy cppcheck gcovr
    gcc-arm-none-eabi binutils-arm-none-eabi
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-user
    python3-pip python3-full python3-venv
    gh git wget curl unzip bzip2 xxd
    openocd dfu-util libusb-1.0-0-dev
    minicom picocom screen
)
MISSING=()
for p in "${APT_PKGS[@]}"; do
    dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" || MISSING+=("$p")
done
if [ ${#MISSING[@]} -gt 0 ]; then
    echo "    installing: ${MISSING[*]}"
    sudo apt-get update -qq
    sudo apt-get install -y -qq "${MISSING[@]}"
else
    echo "    all present"
fi

# ------------------------------------------------------------------ #
# 2. aarch64-none-elf bare-metal toolchain (ZynqMP)                  #
# ------------------------------------------------------------------ #
step "2/6 aarch64-none-elf toolchain"
ARM_DIR=/opt/arm-gnu-toolchain
if [ ! -x "$ARM_DIR/bin/aarch64-none-elf-gcc" ]; then
    fetch https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf.tar.xz \
        /tmp/aarch64-none-elf.tar.xz
    sudo mkdir -p "$ARM_DIR"
    sudo tar -xf /tmp/aarch64-none-elf.tar.xz -C "$ARM_DIR" --strip-components=1
    rm /tmp/aarch64-none-elf.tar.xz
fi
echo "    $("$ARM_DIR/bin/aarch64-none-elf-gcc" --version | head -1)"

# ------------------------------------------------------------------ #
# 3. TI msp430-elf-gcc (MSP430FR5969)                                #
# ------------------------------------------------------------------ #
step "3/6 msp430-elf-gcc toolchain"
MSP_DIR=/opt/msp430-gcc-9.3.1.11_linux64
MSP_URL=https://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export
if [ ! -x "$MSP_DIR/bin/msp430-elf-gcc" ]; then
    fetch "$MSP_URL/msp430-gcc-9.3.1.11_linux64.tar.bz2" /tmp/msp430-gcc.tar.bz2
    sudo tar -xjf /tmp/msp430-gcc.tar.bz2 -C /opt
    rm /tmp/msp430-gcc.tar.bz2
fi
# Device headers go in include/, linker scripts in msp430-elf/lib/ (docs/msp430-build.md)
if [ ! -f "$MSP_DIR/msp430-elf/lib/msp430fr5969.ld" ]; then
    fetch "$MSP_URL/msp430-gcc-support-files-1.212.zip" /tmp/msp430-support.zip
    unzip -q -o /tmp/msp430-support.zip -d /tmp
    sudo cp /tmp/msp430-gcc-support-files/include/*.h "$MSP_DIR/include/"
    sudo cp /tmp/msp430-gcc-support-files/include/*.ld "$MSP_DIR/msp430-elf/lib/"
    rm -rf /tmp/msp430-support.zip /tmp/msp430-gcc-support-files
fi
echo "    $("$MSP_DIR/bin/msp430-elf-gcc" --version | head -1)"

# ------------------------------------------------------------------ #
# 4. Renode (portable: bundles its own .NET runtime)                 #
# ------------------------------------------------------------------ #
step "4/6 Renode"
RENODE_VERSION="1.15.2"
RENODE_DIR=/opt/renode
if ! command -v renode &>/dev/null; then
    fetch "https://github.com/renode/renode/releases/download/v${RENODE_VERSION}/renode-${RENODE_VERSION}.linux-portable.tar.gz" \
        /tmp/renode-portable.tar.gz
    sudo mkdir -p "$RENODE_DIR"
    sudo tar -xf /tmp/renode-portable.tar.gz -C "$RENODE_DIR" --strip-components=1
    sudo ln -sf "$RENODE_DIR/renode" /usr/local/bin/renode
    rm /tmp/renode-portable.tar.gz
fi
echo "    $(renode --version 2>/dev/null | head -1)"

# ------------------------------------------------------------------ #
# 5. Python venv                                                      #
# ------------------------------------------------------------------ #
# A venv hard-codes the path and interpreter it was created with, so the
# devcontainer and native WSL2 each get their own (scripts/env.sh): one made
# on either side never has to be recreated for the other.
step "5/6 Python venv"
if ! "$VENV/bin/python3" -c "import sys" &>/dev/null; then
    echo "    creating ${VENV#"$REPO"/} ($OPENOBSW_ENV)"
    python3 -m venv --clear "$VENV"
fi
"$VENV/bin/pip" install -q --disable-pip-version-check numpy matplotlib
"$VENV/bin/pip" install -q --disable-pip-version-check -e "$REPO/srdb/[dev]"
echo "    $("$VENV/bin/python3" --version) in ${VENV#"$REPO"/}"

# ------------------------------------------------------------------ #
# 6. Host sim build + tests                                           #
# ------------------------------------------------------------------ #
step "6/6 host build + tests"
if [ "$BUILD" -eq 1 ]; then
    # Configure when there is no cache, or when build/ was configured from
    # the other environment's path (a CMake cache is pinned to its source dir).
    cached_src=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' \
                     "$REPO/build/CMakeCache.txt" 2>/dev/null || true)
    if [ "$cached_src" != "$REPO" ]; then
        cmake --fresh -S "$REPO" -B "$REPO/build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Debug \
            -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON \
            -DPython3_EXECUTABLE="$VENV/bin/python3"
    fi
    cmake --build "$REPO/build" -j"$(nproc)"
    ctest --test-dir "$REPO/build" --output-on-failure -q
    "$VENV/bin/pytest" -q "$REPO/srdb/tests"
else
    echo "    skipped (--no-build)"
fi

# ------------------------------------------------------------------ #
# Summary                                                             #
# ------------------------------------------------------------------ #
echo ""
echo "=== tool check ==="
export PATH="$ARM_DIR/bin:$MSP_DIR/bin:$PATH"
for t in cmake ninja gcc clang clang-format clang-tidy cppcheck gcovr \
         arm-none-eabi-gcc aarch64-linux-gnu-gcc aarch64-none-elf-gcc msp430-elf-gcc \
         qemu-aarch64 renode openocd gh xxd; do
    if command -v "$t" &>/dev/null; then printf '    ok       %s\n' "$t"
    else printf '    MISSING  %s\n' "$t"; fi
done
if ! gh auth status &>/dev/null; then
    echo ""
    echo "gh is not logged in. Run once:  gh auth login && gh auth setup-git"
fi

echo ""
echo "=== setup complete — run: source scripts/activate.sh ==="
