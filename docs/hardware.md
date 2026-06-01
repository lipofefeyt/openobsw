# Hardware

Tracked hardware for openobsw and opensvf development. Items are grouped by
priority: **owned**, **ordered**, and **wanted**.

---

## Owned

| Item | Purpose |
|---|---|
| WeAct STM32H750VBT6 | Primary bare-metal OBC target (Cortex-M7, 480 MHz) |
| ST-Link V2 (standalone dongle) | SWD flashing and GDB debugging — no VCP |
| Raspberry Pi (model TBC) | Future opensvf ground segment host |
| CP2102 USB-UART adapter (3.3V/5V) | USART3 console on STM32H750 (PD8=TX, PD9=RX) — arrived 2026-05-29 |
| USB logic analyzer | Debug UART/I2C/SPI traffic — arrived 2026-05-29 |

---

## Wanted

### High priority

| Item | ~Price | Purpose | Notes |
|---|---|---|---|
| QMC5883L magnetometer (GY-273) | €2–4 (AliExpress) | B-dot controller real hardware test — feeds `aocs/bdot.c` directly | Buy QMC5883L specifically, not HMC5883L (discontinued, clones unreliable) |

### Nice to have

| Item | ~Price | Purpose | Notes |
|---|---|---|---|
| MPU-6050 gyro + accel (GY-521) | €3–5 | NOMINAL mode ADCS gyro path | Only useful once a star tracker is also available — low priority for now |

---

## Wiring notes

> **IMPORTANT — 3.3V only.** The STM32H750 GPIO is 3.3V. Set the CP2102
> jumper to 3.3V before connecting. 5V will damage the chip.

---

### ST-Link V2 → STM32H750 (SWD — for flashing)

The WeAct H750 SWD header is on the **opposite side from the USB-C port**,
labeled on the silkscreen. Header pin order (left to right):

```
WeAct SWD header:  GND  |  CLK  |  DIO  |  3.3V
```

ST-Link V2 standalone dongle pinout (10-pin IDC, pin 1 = triangle marker):

```
1  RST     2  SWCLK
3  SWIM    4  SWDIO
5  GND     6  GND
7  3.3V    8  3.3V
9  5.0V   10  5.0V
```

Connections:

```
ST-Link pin 2  (SWCLK)  →  WeAct CLK
ST-Link pin 4  (SWDIO)  →  WeAct DIO
ST-Link pin 5  (GND)    →  WeAct GND
ST-Link pin 7  (3.3V)   →  WeAct 3.3V  (optional — skip if powering from USB-C)
ST-Link pin 1  (RST)    →  WeAct NRST  (optional — improves flash reliability)
```

Flash command — run from **native WSL2**, not the devcontainer terminal.
The devcontainer is a Docker container and does not inherit USB devices forwarded
via usbipd; WSL2 does.

```bash
# In a native WSL2 shell (not VS Code devcontainer):
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program /home/<user>/workspace/openobsw/build_stm32h7/obsw_stm32h7.bin 0x08000000 verify reset exit"
```

`sudo` is not required if your user has access to the ST-Link device node.
If you get `LIBUSB_ERROR_NO_DEVICE`, either run with `sudo` or add a udev rule.

---

### DFU bootloader (alternative — no ST-Link needed)

Enter DFU mode: hold BOOT0 → press+release RESET → wait 0.5s → release BOOT0.
Board appears as a USB DFU device.

```bash
dfu-util -a 0 -s 0x08000000:leave -D build_stm32h7/obsw_stm32h7.bin
```

---

### CP2102 → STM32H750 (USART3 — for serial comms with running firmware)

The firmware uses **USART3 on PD8 (TX) / PD9 (RX)**, 115200 8N1.

```
WeAct PD8  (USART3 TX)  →  CP2102 RX
WeAct PD9  (USART3 RX)  →  CP2102 TX
WeAct GND               →  CP2102 GND
```

Connect from WSL2 (after usbipd attach):
```bash
picocom -b 115200 /dev/ttyUSB0
# exit: Ctrl-A then Ctrl-X
```

---

### WSL2 — forwarding USB devices

USB devices plugged into Windows are not visible in WSL2 by default.
Use **usbipd-win** (install once on Windows, then per session).

**Step 1 — Windows PowerShell (admin):**

```powershell
# List all USB devices and find the bus ID
usbipd list

# Attach to WSL2 (replace X-Y with the bus ID shown)
usbipd attach --wsl --busid X-Y
```

**Step 2 — verify in WSL2:**

```bash
lsusb
```

Known USB IDs for this setup:

| Device | USB ID |
|---|---|
| ST-Link V2 | `0483:3748` — `STMicroelectronics ST-LINK/V2` |
| CP2102 USB-UART | `10c4:ea60` — `Silicon Labs CP210x` |

After attach, the ST-Link is picked up automatically by OpenOCD.
The CP2102 appears as `/dev/ttyUSB0`.

> **Note:** `usbipd attach` must be re-run each time the device is unplugged or
> Windows is rebooted. The `--auto-attach` flag keeps it attached persistently
> across reconnects (usbipd-win ≥ 4.0).

---

### QMC5883L → STM32H750 (I2C — driver not yet implemented)

```
STM32H750 PB8 (SCL)  →  QMC5883L SCL
STM32H750 PB9 (SDA)  →  QMC5883L SDA
STM32H750 3.3V       →  QMC5883L VCC
STM32H750 GND        →  QMC5883L GND
```
