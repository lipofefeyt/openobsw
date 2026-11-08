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

# ── TI msp430-elf-gcc ─────────────────────────────────────────────────
[ -d /opt/msp430-gcc-9.3.1.11_linux64/bin ] && export PATH="/opt/msp430-gcc-9.3.1.11_linux64/bin:$PATH"

# ── aarch64 glibc for QEMU user-mode ─────────────────────────────────
if [ -z "$AARCH64_GLIBC" ]; then
    _GLIBC=$(find /usr -name "ld-linux-aarch64.so.1" 2>/dev/null | head -1 | \
        sed 's|/lib/ld-linux-aarch64.so.1||')
    [ -n "$_GLIBC" ] && export AARCH64_GLIBC="$_GLIBC"
fi

# ── Where am I? (devcontainer vs native WSL2) ────────────────────────
# The ST-Link is only visible in native WSL2 (usbipd), so flashing and the
# OpenOCD server run there; the devcontainer builds and probes over TCP.
if [ -f /.dockerenv ] || [ -n "$REMOTE_CONTAINERS" ] || [ -n "$CODESPACES" ]; then
    export OPENOBSW_ENV=devcontainer
    export OBSW_OCD_HOST="${OBSW_OCD_HOST:-172.17.0.1}"   # WSL2 host via docker bridge
elif grep -qi microsoft /proc/version 2>/dev/null; then
    export OPENOBSW_ENV=wsl
    export OBSW_OCD_HOST="${OBSW_OCD_HOST:-127.0.0.1}"
else
    export OPENOBSW_ENV=linux
    export OBSW_OCD_HOST="${OBSW_OCD_HOST:-127.0.0.1}"
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
# Build anywhere; flash and run the OpenOCD server in native WSL2.
# These used to be aliases; drop old ones so re-sourcing defines the functions.
unalias stm32h7-build stm32h7-build-demo stm32h7-flash stm32h7-ocd \
        stm32h7-probe stm32h7-magmon 2>/dev/null
_obsw_stm32h7_build() {   # $1 = LEOP STANDBY->SAFE timeout [s]
    cmake -S "$OPENOBSW_REPO/targets/stm32h7" -B "$OPENOBSW_REPO/build_stm32h7" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_TOOLCHAIN_FILE="$OPENOBSW_REPO/cmake/stm32h7-toolchain.cmake" \
        -DPython3_EXECUTABLE="$OPENOBSW_PYTHON" \
        -DOBSW_ROOT="$OPENOBSW_REPO" \
        -DOBSW_FREERTOS=ON \
        -DOBSW_STANDBY_SAFE_TIMEOUT_S="$1" > /dev/null \
    && cmake --build "$OPENOBSW_REPO/build_stm32h7" -j"$(nproc)" \
    && echo "[openobsw] build_stm32h7 ready — LEOP STANDBY->SAFE after ${1} s"
}
stm32h7-build()      { _obsw_stm32h7_build 300; }                 # flight-like
stm32h7-build-demo() { _obsw_stm32h7_build "${1:-30}"; }          # bench demo, default 30 s
alias stm32h7-clean='rm -rfv $OPENOBSW_REPO/build_stm32h7'
alias stm32h7-size='arm-none-eabi-size $OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.elf'

_obsw_need_wsl() {
    if [ "$OPENOBSW_ENV" = devcontainer ]; then
        echo "[openobsw] $1 needs the ST-Link, which only native WSL2 can see." >&2
        echo "           Run it in a WSL2 terminal (same repo, same aliases)." >&2
        return 1
    fi
}
# OpenOCD connect-under-reset: hold the board RST button while it connects.
_OBSW_OCD="openocd -f interface/stlink.cfg -c 'adapter speed 480' -f target/stm32h7x.cfg -c 'reset_config none separate'"
stm32h7-flash() {
    _obsw_need_wsl stm32h7-flash || return 1
    local bin="$OPENOBSW_REPO/build_stm32h7/obsw_stm32h7.bin"
    [ -f "$bin" ] || { echo "[openobsw] no $bin — run stm32h7-build first" >&2; return 1; }
    echo "[openobsw] HOLD the board RST button now; release when 'processor detected' appears."
    eval "$_OBSW_OCD -c \"program $bin 0x08000000 verify reset exit\""
}
stm32h7-ocd() {   # OpenOCD server, reachable from the devcontainer for probing
    _obsw_need_wsl stm32h7-ocd || return 1
    echo "[openobsw] HOLD RST, release at 'processor detected'. Ctrl-C to stop (frees the ST-Link)."
    eval "$_OBSW_OCD -c 'bindto 0.0.0.0'"
}
stm32h7-probe()  { python3 "$OPENOBSW_REPO/scripts/stm32h7_probe.py" --host "$OBSW_OCD_HOST" "$@"; }
stm32h7-magmon() {
    local s="$OPENOBSW_REPO/scripts/stm32h7_magmon.py"
    [ -f "$s" ] || { echo "[openobsw] $s not on this branch (it lives on feat/52-hmc5883l, PR #119)" >&2; return 1; }
    python3 "$s" "$OBSW_OCD_HOST" "${1:-60}"
}

# ── Renode ────────────────────────────────────────────────────────────
alias renode-zynqmp='renode $OPENOBSW_REPO/renode/zynqmp_obsw.resc'
alias renode-ping-zynqmp='python3 $OPENOBSW_REPO/renode/test_ping_zynqmp.py'
alias renode-stm32h7='renode $OPENOBSW_REPO/renode/stm32h750_obsw.resc'
alias renode-ping-stm32h7='python3 $OPENOBSW_REPO/renode/test_ping_stm32h7.py'
alias renode-stm32h7-build='cmake -S $OPENOBSW_REPO/targets/stm32h7 \
    -B $OPENOBSW_REPO/build_stm32h7_renode \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE=$OPENOBSW_REPO/cmake/stm32h7-toolchain.cmake \
    -DPython3_EXECUTABLE=$OPENOBSW_PYTHON \
    -DOBSW_ROOT=$OPENOBSW_REPO \
    -DOBSW_RENODE=ON -DOBSW_FREERTOS=ON \
    && cmake --build $OPENOBSW_REPO/build_stm32h7_renode -j$(nproc)'
alias renode-stm32h7-clean='rm -rfv $OPENOBSW_REPO/build_stm32h7_renode'
alias renode-test-leop='python3 $OPENOBSW_REPO/renode/test_leop_stm32h7.py'
alias renode-test-fdir='python3 $OPENOBSW_REPO/renode/test_fdir_stm32h7.py'

# ── Physics harnesses (opensvf-kde boundary) ─────────────────────────────
alias bdot-harness='python3 $OPENOBSW_REPO/sim/bdot_harness.py'
alias adcs-harness='python3 $OPENOBSW_REPO/sim/adcs_harness.py'

# ── Help and one-time setup ──────────────────────────────────────────
obsw-help() {   # print the cheat sheet
    if command -v less >/dev/null 2>&1 && [ -t 1 ]; then
        less -FRX "$OPENOBSW_REPO/docs/cheatsheet.md"
    else
        cat "$OPENOBSW_REPO/docs/cheatsheet.md"
    fi
}
obsw-install() {   # source this file from ~/.bashrc in the current environment (idempotent)
    local line="source $OPENOBSW_REPO/scripts/activate.sh"
    if grep -qxF "$line" ~/.bashrc 2>/dev/null; then
        echo "[openobsw] already in ~/.bashrc ($OPENOBSW_ENV)"
    else
        echo "$line" >> ~/.bashrc && echo "[openobsw] added to ~/.bashrc ($OPENOBSW_ENV) — new terminals get the aliases"
    fi
}

echo "[openobsw] activated ($OPENOBSW_ENV) — repo: $REPO — type obsw-help for the cheat sheet"
