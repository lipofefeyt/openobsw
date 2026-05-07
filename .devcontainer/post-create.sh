#!/usr/bin/env bash
# .devcontainer/post-create.sh
# Runs once when the dev container is first created.
# Sets up all toolchains, builds, and tests.

set -e
echo "=== openobsw dev container setup ==="

# ── System packages ───────────────────────────────────────────────────
sudo apt-get update -qq
sudo apt-get install -y \
    cmake ninja-build \
    gcc clang \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    python3-pip \
    wget curl git \
    minicom picocom \
    openocd \
    dfu-util \
    qemu-user \
    libusb-1.0-0-dev \
    2>/dev/null

echo "[1/4] System packages installed"

# ── aarch64 cross-compiler ────────────────────────────────────────────
if ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
    sudo apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
fi
echo "[2/4] aarch64 toolchain ready"

# ── aarch64-none-elf (bare-metal ZynqMP) ─────────────────────────────
TOOLCHAIN_DIR=/opt/arm-gnu-toolchain
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    echo "    Downloading aarch64-none-elf toolchain..."
    wget -q https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf.tar.xz -O /tmp/aarch64-none-elf.tar.xz
    sudo mkdir -p $TOOLCHAIN_DIR
    sudo tar -xf /tmp/aarch64-none-elf.tar.xz -C $TOOLCHAIN_DIR --strip-components=1
    rm /tmp/aarch64-none-elf.tar.xz
fi
echo "export PATH=$TOOLCHAIN_DIR/bin:\$PATH" >> ~/.bashrc
export PATH=$TOOLCHAIN_DIR/bin:$PATH
echo "[3/4] aarch64-none-elf toolchain ready"

# ── Python venv + SRDB ───────────────────────────────────────────────
cd /workspaces/openobsw
python3 -m venv .venv
source .venv/bin/activate
pip install -q pydantic pyyaml pytest
pip install -q -e /workspaces/openobsw/srdb/
echo "[4/4] Python venv ready"

# ── Build host sim + tests ────────────────────────────────────────────
echo "Building openobsw..."
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON \
    -DOBSW_BUILD_SIM=ON \
    -G Ninja \
    -DPython3_EXECUTABLE=.venv/bin/python3 \
    > /dev/null
cmake --build build -j$(nproc) 2>&1 | tail -3

echo "Running C tests..."
cd build && ctest --output-on-failure -q 2>&1 | tail -3 && cd ..

# ── Activate aliases ──────────────────────────────────────────────────
echo "source $OPENOBSW_REPO/scripts/activate.sh" >> ~/.bashrc

echo ""
echo "=== openobsw container ready ==="
echo "Run: source scripts/activate.sh"