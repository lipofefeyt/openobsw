#!/usr/bin/env bash
# OpenOBSW fast activation — source every terminal
# Usage: source scripts/activate.sh (or auto-sourced from .bashrc)
# Works in: WSL2 native, dev container, GitHub Codespaces

REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)

# ── Python venv ───────────────────────────────────────────────────────
[ -f "$REPO/.venv/bin/activate" ] && source "$REPO/.venv/bin/activate"
export OPENOBSW_REPO="$REPO"
export OPENOBSW_PYTHON="$REPO/.venv/bin/python3"

# ── aarch64-none-elf bare-metal toolchain ────────────────────────────
[ -d /opt/arm-gnu-toolchain/bin ] && export PATH="/opt/arm-gnu-toolchain/bin:$PATH"

# ── aarch64 glibc for QEMU user-mode ─────────────────────────────────
if [ -z "$AARCH64_GLIBC" ]; then
    _GLIBC=$(find /usr -name "ld-linux-aarch64.so.1" 2>/dev/null | head -1 | \
        sed 's|/lib/ld-linux-aarch64.so.1||')
    [ -n "$_GLIBC" ] && export AARCH64_GLIBC="$_GLIBC"
fi

# ── Host sim (x86_64, build/) ────────────────────────────────────────
alias host-build='cmake --build $OPENOBSW_REPO/build -j$(nproc)'
alias host-clean='rm -rfv $OPENOBSW_REPO/build'
alias host-rebuild='host-clean && cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build \
    -DCMAKE_BUILD_TYPE=Debug -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON \
    -G Ninja -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null && host-build'
alias host-test='ctest --test-dir $OPENOBSW_REPO/build --output-on-failure'
alias host-sim='$OPENOBSW_REPO/build/sim/obsw_sim'
alias host-ping='python3 $OPENOBSW_REPO/sim/send_ping.py'

# ── aarch64 Linux sim (build_aarch64/) ───────────────────────────────
alias aarch64-build='cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build_aarch64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/aarch64-linux-gnu.cmake \
    -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=ON -G Ninja \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null \
    && cmake --build $OPENOBSW_REPO/build_aarch64 -j$(nproc) 2>&1 | tail -3'
alias aarch64-clean='rm -rfv $OPENOBSW_REPO/build_aarch64'
alias aarch64-sim='qemu-aarch64 -L $AARCH64_GLIBC $OPENOBSW_REPO/build_aarch64/sim/obsw_sim'

# ── ZynqMP bare-metal (build_zynqmp_baremetal/) ──────────────────────
alias zynqbare-build='cmake --build $OPENOBSW_REPO/build_zynqmp_baremetal -j$(nproc)'
alias zynqbare-clean='rm -rfv $OPENOBSW_REPO/build_zynqmp_baremetal'

# ── STM32H7 (build_stm32h7/) ─────────────────────────────────────────
alias stm32h7-build='cmake -S $OPENOBSW_REPO/targets/stm32h7 \
    -B $OPENOBSW_REPO/build_stm32h7 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/stm32h7-toolchain.cmake \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON \
    -DOBSW_ROOT=$OPENOBSW_REPO > /dev/null \
    && cmake --build $OPENOBSW_REPO/build_stm32h7 -j$(nproc)'
alias stm32h7-clean='rm -rfv $OPENOBSW_REPO/build_stm32h7'
alias stm32h7-flash='openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program $OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"'
alias stm32h7-size='arm-none-eabi-size $OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.elf'

# ── Renode ────────────────────────────────────────────────────────────
alias renode-zynqmp='renode $OPENOBSW_REPO/renode/zynqmp_obsw.resc'
alias renode-ping='python3 $OPENOBSW_REPO/renode/test_ping_zynqmp.py'

echo "[openobsw] activated — repo: $REPO"
echo "[openobsw] aliases: host-build host-test host-sim | aarch64-build | zynqbare-build | stm32h7-build stm32h7-flash | renode-zynqmp"