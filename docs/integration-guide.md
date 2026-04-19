# SVF Integration Guide for Validation Engineers

> **Audience:** Spacecraft software validation engineers  
> **Time to first campaign:** ~4 hours  
> **Prerequisites:** Python 3.10+, your OBSW binary or source

---

## Overview

OpenSVF validates your flight software against a 6-DOF physics engine and a real ground station. This guide takes you from zero to running a test campaign against your OBSW.

---

## Step 1: Installation

```bash
git clone https://github.com/lipofefeyt/opensvf
cd opensvf
source scripts/setup-workspace.sh
testosvf   # should show ~400 tests passing
```

---

## Step 2: Adapt Your OBSW

SVF communicates with your OBSW via a simple pipe protocol. Copy `contrib/svf_protocol/` from the openobsw repository into your project.

Implement your main loop:

```c
#include "obsw_svf_protocol.h"

int main(void) {
    svf_init("1.0.0");  // prints startup banner

    uint8_t frame[256];
    uint16_t frame_len;

    while (1) {
        uint8_t type = svf_read_frame(frame, sizeof(frame), &frame_len);

        if (type == SVF_FRAME_TC) {
            // Dispatch PUS telecommand
            obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
        }
        else if (type == SVF_FRAME_SENSOR) {
            // Unpack sensor data and run your control algorithm
            svf_sensor_frame_t *s = (svf_sensor_frame_t *)frame;
            run_aocs(s);

            // Send actuator commands back to SVF
            svf_actuator_frame_t act = {0};
            act.mtq_dipole_x = my_mtq_cmd.x;
            act.sim_time     = s->sim_time;
            svf_write_actuator(&act);
        }

        // Drain TM packets, then send end-of-tick
        drain_tm_store();
        svf_write_sync();
    }
}
```

### Sensor frame layout (SVF → OBSW, type 0x02)

```c
typedef struct {
    float   mag_x, mag_y, mag_z;              // Magnetometer [T]
    uint8_t mag_valid;
    float   st_q_w, st_q_x, st_q_y, st_q_z;  // Quaternion
    uint8_t st_valid;
    float   gyro_x, gyro_y, gyro_z;           // Gyroscope [rad/s]
    uint8_t gyro_valid;
    float   sim_time;                          // Simulation time [s]
} svf_sensor_frame_t;  // 29 bytes, little-endian
```

### Actuator frame layout (OBSW → SVF, type 0x03)

```c
typedef struct {
    float   mtq_dipole_x, mtq_dipole_y, mtq_dipole_z;  // [Am²]
    float   rw_torque_x,  rw_torque_y,  rw_torque_z;   // [Nm]
    uint8_t controller;   // 0 = b-dot, 1 = ADCS PD
    float   sim_time;
} svf_actuator_frame_t;  // 29 bytes, little-endian
```

### Build your host simulation binary

```bash
gcc -std=c11 -o obsw_sim \
    src/main.c \
    src/svf_protocol/obsw_svf_protocol.c \
    src/svf_protocol/platform/linux_stdio.c \
    src/aocs/bdot.c src/pus/*.c \
    -I include/

# Test it starts correctly
echo "" | ./obsw_sim
# [OBSW] Host sim started (type-frame protocol v2).
# [OBSW] SRDB version: 1.0.0
```

---

## Step 3: Configure Your Spacecraft

Create `spacecraft.yaml` in your project root:

```yaml
spacecraft: MySat-1

obsw:
  type: pipe
  binary: ./obsw_sim

equipment:
  - id: kde
    model: dynamics

  - id: mag1
    model: magnetometer
    hardware_profile: mag_default

  - id: gyro1
    model: gyroscope
    hardware_profile: gyro_default

  - id: mtq1
    model: magnetorquer

  - id: rw1
    model: reaction_wheel
    hardware_profile: rw_default

wiring:
  auto: true    # auto-connects matching port names

simulation:
  dt: 0.1
  stop_time: 300.0
  seed: 42      # deterministic replay
```

Available models: `dynamics`, `magnetometer`, `magnetorquer`, `gyroscope`,
`star_tracker`, `css`, `reaction_wheel`, `thruster`, `gps`, `thermal`,
`pcdu`, `sbt`.

Hardware profiles available in `srdb/data/hardware/`.

---

## Step 4: Write Test Procedures

Create `tests/procedures/test_aocs.py`:

```python
from svf.procedure import Procedure, ProcedureContext


class BdotConvergence(Procedure):
    id          = "TC-AOCS-001"
    title       = "B-dot detumbling convergence"
    requirement = "MIS-AOCS-042"

    def run(self, ctx: ProcedureContext) -> None:
        self.step("Power on AOCS sensors")
        ctx.inject("aocs.mag.power_enable", 1.0)
        ctx.inject("aocs.gyro.power_enable", 1.0)

        self.step("Wait for detumbling")
        ctx.wait(60.0)

        self.step("Verify angular rate converged")
        ctx.assert_parameter(
            "aocs.truth.rate_x",
            less_than=0.1,
            requirement="MIS-AOCS-042",
        )


class PingPong(Procedure):
    id          = "TC-COM-001"
    title       = "OBC are-you-alive ping/pong"
    requirement = "MIS-COM-001"

    def run(self, ctx: ProcedureContext) -> None:
        self.step("Send TC(17,1) ping")
        ctx.tc(17, 1)

        self.step("Expect TM(17,2) pong within 5s")
        ctx.expect_tm(17, 2, timeout=5.0)
```

---

## Step 5: Create a Campaign

Create `campaign.yaml`:

```yaml
campaign: MySat-1 AOCS Validation
spacecraft: spacecraft.yaml
procedures:
  - tests/procedures/test_aocs.py
```

---

## Step 6: Run the Campaign

```python
from svf.campaign_runner import CampaignRunner
from svf.report import generate_html_report
from pathlib import Path

runner = CampaignRunner.from_yaml("campaign.yaml")
report = runner.run()
generate_html_report(report, Path("results/report.html"))
print(f"Pass rate: {report.pass_rate*100:.1f}%")
```

Open `results/report.html` in your browser — it is fully self-contained,
no internet connection required.

---

## OBSW Transport Options

| Transport | When to use |
|---|---|
| `pipe` (stdin/stdout) | Development, CI, fast iteration |
| `socket` (Renode TCP) | Peripheral-level SIL with Cadence UART emulation |
| `stub` (no binary) | Unit testing, campaign scaffolding |

### Renode socket mode

```bash
# In openobsw: build bare-metal binary
baremetal-build

# Start Renode
renode renode/zynqmp_obsw.resc &
sleep 5
```

```yaml
obsw:
  type: socket
  host: localhost
  port: 3456
```

---

## Troubleshooting

**"obsw_sim not found"**  
Check the `binary:` path in `spacecraft.yaml`. Use an absolute path or
run from the directory containing the binary.

**"SRDB version mismatch"**  
Rebuild your OBSW with the current SRDB, or update the SVF SRDB to match
your OBSW version.

**"Timeout waiting for TM(17,2)"**  
Check that: (1) your TC dispatcher is initialised, (2) `svf_write_sync()`
is called at the end of every tick, (3) the APID is `0x010`.

**"Parameter not found in ParameterStore"**  
Check canonical parameter names in `docs/equipment-library.md`. After a
run, call `store.snapshot()` to print all available parameters.

**"Auto-wiring inferred 0 connections"**  
Your equipment port names do not match SVF canonical names. Use
`wiring.overrides` to map your names explicitly.

---

## Reference Documentation

| Document | Contents |
|---|---|
| `docs/equipment-library.md` | All equipment port names and physics |
| `docs/architecture.md` | System architecture and wire protocol |
| `docs/design-philosophy.md` | What SVF is and is not |
| `contrib/svf_protocol/obsw_svf_protocol.h` | C protocol reference |