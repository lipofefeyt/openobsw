#!/usr/bin/env bash
# .devcontainer/post-create.sh
# Works in both WSL2 native and dev container contexts.

set -e

# Resolve repo root regardless of context
REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
cd "$REPO"

echo "=== openobsw dev container setup ==="
echo "    Repo: $REPO"

# ── System packages ───────────────────────────────────────────────────
sudo apt-get update -qq
sudo apt-get install -y \
    cmake ninja-build \
    gcc clang \
    gcc-arm-none-eabi binutils-arm-none-eabi \
    python3-pip python3-full python3-venv \
    wget curl git \
    minicom picocom \
    openocd \
    dfu-util \
    qemu-user \
    libusb-1.0-0-dev \
    screen \
    2>/dev/null
echo "[1/5] System packages installed"

# ── aarch64 Linux cross-compiler ─────────────────────────────────────
if ! command -v aarch64-linux-gnu-gcc &>/dev/null; then
    sudo apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
fi
echo "[2/5] aarch64-linux-gnu toolchain ready"

# ── aarch64-none-elf bare-metal toolchain ────────────────────────────
TOOLCHAIN_DIR=/opt/arm-gnu-toolchain
if [ ! -f "$TOOLCHAIN_DIR/bin/aarch64-none-elf-gcc" ]; then
    echo "    Downloading aarch64-none-elf toolchain (~150MB)..."
    wget -q https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf.tar.xz \
        -O /tmp/aarch64-none-elf.tar.xz
    sudo mkdir -p $TOOLCHAIN_DIR
    sudo tar -xf /tmp/aarch64-none-elf.tar.xz -C $TOOLCHAIN_DIR --strip-components=1
    rm /tmp/aarch64-none-elf.tar.xz
fi
export PATH="$TOOLCHAIN_DIR/bin:$PATH"
echo "export PATH=$TOOLCHAIN_DIR/bin:\$PATH" >> ~/.bashrc
echo "[3/5] aarch64-none-elf toolchain ready"

# ── Python venv ───────────────────────────────────────────────────────
if [ ! -f "$REPO/.venv/bin/python3" ]; then
    python3 -m venv "$REPO/.venv"
fi
source "$REPO/.venv/bin/activate"
pip install -q pydantic pyyaml pytest
pip install -q -e "$REPO/srdb/"
echo "[4/6] Python venv ready"

# ── Renode ────────────────────────────────────────────────────────────
# Use the portable tarball: bundles its own .NET/Mono runtime so it works
# on Ubuntu 24.04 which dropped the mono-* and gtk-sharp2 packages that
# the .deb version depends on.
RENODE_VERSION="1.15.2"
RENODE_DIR="/opt/renode"
if ! command -v renode &>/dev/null; then
    echo "    Downloading Renode ${RENODE_VERSION} (portable)..."
    wget -q "https://github.com/renode/renode/releases/download/v${RENODE_VERSION}/renode-${RENODE_VERSION}.linux-portable.tar.gz" \
        -O /tmp/renode-portable.tar.gz
    sudo mkdir -p "$RENODE_DIR"
    sudo tar -xf /tmp/renode-portable.tar.gz -C "$RENODE_DIR" --strip-components=1
    sudo ln -sf "$RENODE_DIR/renode" /usr/local/bin/renode
    rm /tmp/renode-portable.tar.gz
fi
echo "[5/6] Renode ready ($(renode --version 2>/dev/null | head -1))"

# ── Build + test ──────────────────────────────────────────────────────
cmake -S "$REPO" -B "$REPO/build" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DOBSW_BUILD_TESTS=ON \
    -DOBSW_BUILD_SIM=ON \
    -G Ninja \
    -DPython3_EXECUTABLE="$REPO/.venv/bin/python3" \
    > /dev/null

cmake --build "$REPO/build" -j$(nproc) 2>&1 | tail -3
cd "$REPO/build" && ctest --output-on-failure -q 2>&1 | tail -3
cd "$REPO"
echo "[6/6] Build and tests complete"

# ── Aliases ───────────────────────────────────────────────────────────
echo "source $REPO/scripts/activate.sh" >> ~/.bashrc

# ── Git identity (persists in container) ─────────────────────────────
if [ -z "$(git config --global user.email)" ]; then
    echo "⚠  Git identity not set. Run:"
    echo "   git config --global user.email 'you@example.com'"
    echo "   git config --global user.name 'lipofefeyt'"
fi

# ── Global CLAUDE.md ──────────────────────────────────────────────────
mkdir -p /home/vscode/.claude-global
touch /home/vscode/.claude-global/CLAUDE.md
ln -sf /home/vscode/.claude-global/CLAUDE.md /home/vscode/.claude/CLAUDE.md
echo "[+] Global CLAUDE.md linked from WSL2 host"

echo ""
echo "=== openobsw ready ==="
echo "Run: source scripts/activate.sh"
