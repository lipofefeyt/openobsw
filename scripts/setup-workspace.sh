#!/bin/bash
# setup-workspace.sh — Restore full openobsw dev environment
#
# Run once after cloning or after a workspace reset:
#   bash scripts/setup-workspace.sh
#   source ~/.bashrc

set -e
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "=== [1/5] Installing apt packages ==="
sudo apt-get update -q
sudo apt-get install -y \
    build-essential \
    git \
    python3 \
    python3-full \
    python3-pip \
    python3-venv \
    python3.12-venv \
    binutils-msp430 \
    msp430mcu \
    mspdebug \
    unzip \
    wget \
    curl 2>/dev/null || true

# Install cmake separately — fall back to pip if apt fails
if ! command -v cmake &>/dev/null; then
    echo "cmake not found via apt, installing via pip..."
    pip3 install cmake --break-system-packages
fi

# Verify cmake
cmake --version || { echo "ERROR: cmake install failed"; exit 1; }

echo "=== [2/5] Setting up Python venv ==="
python3 -m venv .venv
source .venv/bin/activate
pip install --quiet --upgrade pip
pip install --quiet pydantic pyyaml pytest

echo "=== [3/5] Installing MSP430 GCC toolchain ==="
if [ ! -f /opt/msp430-gcc-9.3.1.11_linux64/bin/msp430-elf-gcc ]; then
    echo "Downloading msp430-elf-gcc 9.3.1.11 (~70MB)..."
    wget -q "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-9.3.1.11_linux64.tar.bz2"
    wget -q "http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSPGCC/9_3_1_2/export/msp430-gcc-support-files-1.212.zip"
    sudo tar xjf msp430-gcc-9.3.1.11_linux64.tar.bz2 -C /opt
    unzip -q msp430-gcc-support-files-1.212.zip
    find msp430-gcc-support-files -name "*.h"  -exec sudo cp {} /opt/msp430-gcc-9.3.1.11_linux64/include/ \;
    find msp430-gcc-support-files -name "*.ld" -exec sudo cp {} /opt/msp430-gcc-9.3.1.11_linux64/msp430-elf/lib/ \;
    rm -f msp430-gcc-9.3.1.11_linux64.tar.bz2 msp430-gcc-support-files-1.212.zip
    rm -rf msp430-gcc-support-files
    echo "MSP430 GCC installed."
else
    echo "MSP430 GCC already installed, skipping."
fi

echo "=== [4/5] Installing Renode nightly ==="
RENODE_DIR="/opt/renode-nightly"
if [ ! -f "$RENODE_DIR/renode" ]; then
    echo "Downloading Renode v1.15.3..."
    wget -q "https://github.com/renode/renode/releases/download/v1.15.3/renode-1.15.3.linux-portable.tar.gz" -O renode.tar.gz
    sudo mkdir -p "$RENODE_DIR"
    sudo tar xf renode.tar.gz -C "$RENODE_DIR" --strip-components=1
    rm -f renode.tar.gz
    echo "Renode installed."
else
    echo "Renode already installed, skipping."
fi

echo "=== [5/5] Writing ~/.bashrc aliases ==="
BASHRC="$HOME/.bashrc"
# Remove stale openobsw block if present
sed -i '/# >>> openobsw/,/# <<< openobsw/d' "$BASHRC" 2>/dev/null || true

cat >> "$BASHRC" << 'ALIASES'

# >>> openobsw >>>
export PATH=/opt/msp430-gcc-9.3.1.11_linux64/bin:/opt/renode-nightly:$PATH

# Auto-activate venv
if [ -f "/workspaces/openobsw/.venv/bin/activate" ]; then
    source "/workspaces/openobsw/.venv/bin/activate"
fi

# Host build
alias omkdebug='cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
alias omkbuild='cmake --build build'
alias otest='ctest --test-dir build --output-on-failure'
alias oclean='rm -rf build'

# MSP430 cross-compilation
alias mkdebug-msp430='cmake -B build-msp430 -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/msp430-toolchain.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -S targets/msp430-fr5969'
alias mkbuild-msp430='cmake --build build-msp430'
alias mclean-msp430='rm -rf build-msp430'
alias mflash-msp430='mspdebug rf2500 "prog build-msp430/openobsw-msp430.elf"'
alias msize-msp430='msp430-elf-size build-msp430/openobsw-msp430.elf'

# Renode build
alias mkdebug-renode='cmake -B build-renode -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/msp430-toolchain.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DRENODE_BUILD=ON -S targets/msp430-fr5969'
alias mkbuild-renode='cmake --build build-renode'
alias mclean-renode='rm -rf build-renode'

# Misc
alias bashed='vim ~/.bashrc'
alias bashmk='source ~/.bashrc'
# <<< openobsw <<<
ALIASES

source "$BASHRC"

echo ""
echo "============================================"
echo " Setup complete!"
echo "============================================"
echo ""
echo " Run: source ~/.bashrc"
echo ""
echo " Then verify:"
echo "   python3 --version"
echo "   cmake --version"
echo "   msp430-elf-gcc --version"
echo "   renode --version"
echo ""
echo " Build:"
echo "   cd /workspaces/openobsw"
echo "   omkdebug && omkbuild && otest"
echo "   mkdebug-msp430 && mkbuild-msp430"
echo "   mkdebug-renode && mkbuild-renode"
echo "============================================"