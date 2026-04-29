#!/usr/bin/env bash
# scripts/activate.sh
#
# Source every terminal. Fast — no installs, no builds.
# Setup is handled once by scripts/setup-workspace.sh

[ -n "$ASCIINEMA_REC" ] && return 0

REPO=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)

# ── Environment ──────────────────────────────────────────────────────
export OPENOBSW_REPO="$REPO"
export OPENOBSW_PYTHON="$REPO/.venv/bin/python3"

printf "[openobsw] activating... "

# Venv
[ -f "$REPO/.venv/bin/activate" ] && source "$REPO/.venv/bin/activate"

# Nix profile
[ -d "$HOME/.nix-profile/bin" ] && export PATH="$HOME/.nix-profile/bin:$PATH"

# aarch64 glibc — cache in env to avoid slow find on every terminal
if [ -z "$AARCH64_GLIBC" ]; then
    _GLIBC=$(find /nix/store -name "ld-linux-aarch64.so.1" \
        -path "*/glibc-aarch64*" 2>/dev/null | head -1 | \
        sed 's|/lib/ld-linux-aarch64.so.1||')
    [ -n "$_GLIBC" ] && export AARCH64_GLIBC="$_GLIBC"
fi

# ── Host sim (x86_64, build/) ────────────────────────────────────────
alias host-build='cmake --build $OPENOBSW_REPO/build -j$(nproc)'
alias host-clean='rm -rfv $OPENOBSW_REPO/build && echo "host build cleaned"'
alias host-rebuild='host-clean && cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build \
    -DCMAKE_BUILD_TYPE=Debug -DOBSW_BUILD_TESTS=ON -DOBSW_BUILD_SIM=ON \
    -G "Unix Makefiles" -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null \
    && host-build'
alias host-test='cd $OPENOBSW_REPO/build && ctest --output-on-failure && cd $OPENOBSW_REPO'
alias host-sim='$OPENOBSW_REPO/build/sim/obsw_sim'
alias host-ping='python3 $OPENOBSW_REPO/sim/send_ping.py'

# ── aarch64 Linux sim (build_aarch64/) ───────────────────────────────
alias aarch64-build='cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build_aarch64 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/aarch64-linux-gnu.cmake \
    -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=ON -G "Unix Makefiles" \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null \
    && cmake --build $OPENOBSW_REPO/build_aarch64 -j$(nproc) 2>&1 | tail -3'
alias aarch64-clean='rm -rfv $OPENOBSW_REPO/build_aarch64 && echo "aarch64 build cleaned"'
alias aarch64-sim='qemu-aarch64 -L $AARCH64_GLIBC $OPENOBSW_REPO/build_aarch64/sim/obsw_sim'
alias aarch64-file='file $OPENOBSW_REPO/build_aarch64/sim/obsw_sim'

# ── ZynqMP Linux sim (build_zynqmp/) ─────────────────────────────────
alias zynqmp-build='cmake -S $OPENOBSW_REPO -B $OPENOBSW_REPO/build_zynqmp \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/aarch64-linux-gnu.cmake \
    -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=ON -G "Unix Makefiles" \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON > /dev/null \
    && cmake --build $OPENOBSW_REPO/build_zynqmp -j$(nproc) 2>&1 | tail -3'
alias zynqmp-clean='rm -rfv $OPENOBSW_REPO/build_zynqmp && echo "zynqmp build cleaned"'

# ── ZynqMP bare-metal (build_zynqmp_baremetal/) ───────────────────────
alias zynqbare-build='cmake --build $OPENOBSW_REPO/build_zynqmp_baremetal -j$(nproc)'
alias zynqbare-clean='rm -rfv $OPENOBSW_REPO/build_zynqmp_baremetal && echo "zynqbare build cleaned"'
alias zynqbare-objcopy='/tmp/arm-gnu-toolchain-12.3.rel1-x86_64-aarch64-none-elf/bin/aarch64-none-elf-objcopy \
    -O binary \
    $OPENOBSW_REPO/build_zynqmp_baremetal/obsw_zynqmp \
    $OPENOBSW_REPO/build_zynqmp_baremetal/obsw_zynqmp.bin'

# ── STM32H7 (build_stm32h7/) ─────────────────────────────────────────
alias stm32h7-build='cmake -S $OPENOBSW_REPO/targets/stm32h7 \
    -B $OPENOBSW_REPO/build_stm32h7 \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/stm32h7-toolchain.cmake \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON \
    -DOBSW_ROOT=$OPENOBSW_REPO > /dev/null \
    && cmake --build $OPENOBSW_REPO/build_stm32h7 -j$(nproc)'
alias stm32h7-clean='rm -rfv $OPENOBSW_REPO/build_stm32h7 && echo "stm32h7 build cleaned"'
alias stm32h7-flash='dfu-util -a 0 -s 0x08000000:leave \
    -D $OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.bin'
alias stm32h7-size='arm-none-eabi-size $OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.elf'

# ── Renode ────────────────────────────────────────────────────────────
alias renode-zynqmp='renode $OPENOBSW_REPO/renode/zynqmp_obsw.resc'
alias renode-ping='python3 $OPENOBSW_REPO/renode/test_ping_zynqmp.py'

echo "done"
echo "[openobsw] targets: host | aarch64 | zynqmp | zynqbare | stm32h7"
echo "[openobsw] build/  : $(ls $OPENOBSW_REPO/build/sim/obsw_sim 2>/dev/null && echo 'obsw_sim ✓' || echo 'not built — run host-rebuild')"