# openobsw cheat sheet

`obsw-help` prints this file. All commands come from `scripts/activate.sh`,
which works the same in the **devcontainer** (VS Code terminal) and in
**native WSL2**.

## One-time setup

| Where | Do |
|---|---|
| Devcontainer | Nothing. `post-create.sh` already sources `activate.sh`. |
| WSL2 | `bash ~/workspace/openobsw/scripts/setup-workspace.sh --no-build` once (tools and `.venv-wsl`), then `source ~/workspace/openobsw/scripts/activate.sh && obsw-install` |

`obsw-install` adds the `source` line to `~/.bashrc`. It's safe to run twice.
The banner shows where you are: `activated (devcontainer)` or `activated (wsl)`.

Both use the same working tree but not the same Python venv: `.venv` belongs
to the devcontainer, `.venv-wsl` to WSL2. `$OPENOBSW_PYTHON` is the right one.
`build/` is shared and belongs to whichever side configured it last.

## Where to run what

| Task | Devcontainer | WSL2 |
|---|:-:|:-:|
| Build, tests, host sim, Renode | ✅ | ✅ |
| **Flash / OpenOCD server** (the ST-Link is only visible in WSL2) | ❌ | ✅ |
| Probe the running board (`stm32h7-probe`, `stm32h7-magmon`) | ✅ (via `172.17.0.1`) | ✅ |

## Host

| Command | Does |
|---|---|
| `host-rebuild` | Clean configure and build of `build/` (tests and sim) |
| `host-build` | Incremental build. Refuses if `build/` was configured in the other environment; run `host-rebuild`. |
| `host-test` | All ctest tests (`ctest --test-dir build -R <name>` for one) |
| `host-sim` / `host-ping` | Run the sim / send it a TC(17,1) |

## STM32H750 (WeAct board)

| Command | Does |
|---|---|
| `stm32h7-build` | Flight-like image: LEOP STANDBY→SAFE after **300 s** |
| `stm32h7-build-demo [s]` | Demo image: STANDBY→SAFE after **30 s** (or `[s]`) |
| `stm32h7-flash` | **WSL2.** Flash `build_stm32h7`. **Hold RST** until `processor detected`. |
| `stm32h7-ocd` | **WSL2.** OpenOCD server for probing (hold RST the same way). Ctrl-C frees the ST-Link. |
| `stm32h7-probe` | Show the LCD frame the firmware drew, reset flags and uptime, without halting the CPU |
| `stm32h7-magmon [s]` | Live I2C magnetometer check (on the `feat/52-hmc5883l` branch) |
| `stm32h7-size` | Flash/RAM use |

**Typical bench loop:** `stm32h7-build-demo` (either terminal) → `stm32h7-flash` (WSL2) → watch the LCD.
To look inside: `stm32h7-ocd` (WSL2), then `stm32h7-probe` (either terminal).

Build options (`cmake -S targets/stm32h7 -B build_stm32h7 -D...`):
`OBSW_STANDBY_SAFE_TIMEOUT_S` (300) · `OBSW_UART_ASCII_DEBUG` (OFF) · `OBSW_LCD_KEEPALIVE` (OFF, legacy) · `OBSW_RENODE` · `OBSW_FREERTOS`

## Reading the LCD dashboard

```
openobsw v0.18.0 STM32H750      version (obsw/version.h)
OBT 00000123  RST POR           on-board time · reset cause
TC rx 000000  rej 000000        TCs received · TM(1,2)+(1,8) sent
TM tx 000002  last 5,4          TMs sent · last TM (svc,subsvc)
EVT 0x0002 HIGH                 last event: 0x0001 BOOT, 0x0002 SAFE entry
AOCS BDOT     MAG ok            OFF (STANDBY) · IDLE (no valid sensor) · BDOT · PD
B uT   +21.3   -4.0  +38.9      magnetic field
M Am2  +0.12  -0.03  +0.40      commanded dipole (computed, not actuated)
UP 000:02:03  WDG 00000123      uptime · watchdog kicks
MODE SAFE                       yellow STANDBY · red SAFE · green NOMINAL
```

| `RST` | Meaning |
|---|---|
| `POR` | Power plugged in |
| `PIN` | RST button |
| `SW` | Software reset, e.g. after flashing |
| `IWDG` | **Watchdog reset: something hung** |
| `BOR` | Supply dip |

After boot the board is in STANDBY with AOCS `OFF`. With no ground contact
it goes to SAFE after the LEOP timeout: red bar, `EVT 0x0002 HIGH`. That is
designed behaviour, not a fault.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `no venv at .../.venv-wsl` (or `.venv`) on activation | `bash scripts/setup-workspace.sh --no-build` in this environment. |
| `build/ was configured from ...` | The other environment configured `build/` last. `host-rebuild`. |
| `No module named 'obsw_srdb'` | Wrong Python: something activated another venv after `activate.sh`. Use `$OPENOBSW_PYTHON -m pytest ...` or re-source `activate.sh`. |
| `init mode failed (unable to connect to the target)` | Hold **RST** while OpenOCD starts; the aliases already use `reset_config none separate` and 480 kHz. |
| `program` can't find the file | Use `$HOME/...`, not `~`, inside `-c "..."` (the aliases do this). |
| Probe: `failed to read memory` | The board reset or the SWD wires moved. Restart `stm32h7-ocd`, holding RST. |
| Probe: `cannot reach OpenOCD` | Start `stm32h7-ocd` in WSL2 first. |
| `MAG --` in SAFE | Sensor not detected at boot. Is it soldered and wired to PB8/PB9, VCC and GND? Reset after wiring. See `docs/hardware.md`. |
| LCD black | Backlight PE10 is **active-low**; check `lcd.c` `BL_ON`/`BL_OFF`. |

## Git / GitHub

| Command | Does |
|---|---|
| `git checkout -b feat/<issue>-<topic> main` | Branch for an issue |
| `gh pr create --base main` | Open a PR (CI: gcc/clang, ASAN, coverage ≥ 90 %, MISRA, trace) |
| `gh pr checks <n> --watch` | Watch CI |
| Release | Bump `include/obsw/version.h` (CMake reads it), add a CHANGELOG section, tag `vX.Y` |

More detail: `docs/hardware.md` (wiring, flashing), `docs/freertos-architecture.md`,
`docs/architecture.md`, `CLAUDE.md`.
