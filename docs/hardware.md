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

---

## Ordered

| Item | Purpose | Link |
|---|---|---|
| CP2102 USB-UART adapter (3.3V/5V) | USART3 console on STM32H750 (PD8=TX, PD9=RX) | [Amazon.fr](https://www.amazon.fr/-/en/RUIZHI-Converter-Compatible-Arduino-Download/dp/B0DXL2G5K4/) |

---

## Wanted

### High priority

| Item | ~Price | Purpose | Notes |
|---|---|---|---|
| QMC5883L magnetometer (GY-273) | €2–4 (AliExpress) | B-dot controller real hardware test — feeds `aocs/bdot.c` directly | Buy QMC5883L specifically, not HMC5883L (discontinued, clones unreliable) |

### Nice to have

| Item | ~Price | Purpose | Notes |
|---|---|---|---|
| USB logic analyzer (8ch, 24 MHz) | €10–15 | Debug UART/I2C/SPI without a scope | Any Cypress FX2-based clone works with Sigrok/PulseView on Linux |
| MPU-6050 gyro + accel (GY-521) | €3–5 | NOMINAL mode ADCS gyro path | Only useful once a star tracker is also available — low priority for now |

---

## Wiring notes

### CP2102 → STM32H750 (USART3)

```
STM32H750 PD8 (TX)  →  CP2102 RX
STM32H750 PD9 (RX)  →  CP2102 TX
STM32H750 GND       →  CP2102 GND
```

Set adapter to **3.3V** logic (STM32H750 GPIO is 3.3V — 5V will damage it).

Connect with: `screen /dev/ttyUSB0 115200`

### QMC5883L → STM32H750 (I2C — TBD)

I2C HAL driver for STM32H750 not yet implemented. Wiring and driver work
planned once the module arrives.

```
STM32H750 PB8 (SCL)  →  QMC5883L SCL
STM32H750 PB9 (SDA)  →  QMC5883L SDA
STM32H750 3.3V       →  QMC5883L VCC
STM32H750 GND        →  QMC5883L GND
```
