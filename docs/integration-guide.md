# OpenSVF integration

This page covers the OpenOBSW side of a validation setup: driving the host
simulator yourself, adding the wire protocol to another OBSW, and running a
target under Renode. Setting up OpenSVF itself (spacecraft YAML, equipment
models, procedures, campaigns, reports) is documented by OpenSVF:
[opensvf.readthedocs.io](https://opensvf.readthedocs.io/).

The bytes on the wire are specified on the [Wire protocol](icd.md) page.

---

## Driving the host sim without OpenSVF

The host sim, `obsw_sim`, speaks the wire protocol on stdin and stdout, so
any language can drive it.

### Quick check

```bash
# Build the host sim
cmake -S . -B build -DOBSW_BUILD_SIM=ON -DOBSW_BUILD_TESTS=ON -G Ninja
cmake --build build -j$(nproc)

# Send a TC(17,1) ping, close stdin so the sim exits, print what came back
python3 - <<'EOF'
import subprocess, struct

def tc_frame(apid, svc, subsvc):
    sp  = struct.pack(">HHH",
        0x1800 | (apid & 0x7FF),   # packet ID: TC, secondary header
        0xC000,                     # standalone, sequence count 0
        4)                          # data length: 5-byte secondary header - 1
    sp += bytes([0x11, svc, subsvc, 0x00, 0x00])  # PUS-C TC secondary header
    return struct.pack(">BH", 0x01, len(sp)) + sp  # wire frame type 0x01

out = subprocess.run(["./build/sim/obsw_sim"], input=tc_frame(0x010, 17, 1),
                     capture_output=True).stdout
print("Raw response:", out.hex())
EOF
```

The response is TM(1,1), TM(17,2) and TM(1,7), each as a `0x04` frame, then
`0xFF`.

### A minimal reader

Every frame the sim sends is `[type][uint16 BE length][body]`, except the
end-of-tick byte `0xFF`:

```python
import struct

END_OF_TICK = 0xFF
FRAME_ACTUATOR, FRAME_TM = 0x03, 0x04

def read_tick(stream):
    """Read the frames of one tick, up to and including 0xFF."""
    tms, actuator = [], None
    while True:
        t = stream.read(1)
        if not t or t[0] == END_OF_TICK:
            return tms, actuator
        length = struct.unpack(">H", stream.read(2))[0]
        body = stream.read(length)
        if t[0] == FRAME_TM:
            tms.append(body)
        elif t[0] == FRAME_ACTUATOR:
            actuator = body
```

Send one frame (a TC or a sensor frame), then call `read_tick()`: the host
sim answers every frame with exactly one `0xFF`. `sim/wire_proto.py` and
`test/system/conftest.py` are complete examples.

---

## Adding the wire protocol to another OBSW

`contrib/svf_protocol/` is a drop-in reference implementation of the wire
protocol for an OBSW other than OpenOBSW: `obsw_svf_protocol.h` and `.c`,
plus platform glue for Linux stdio and the ZynqMP UART. `tools/gen_docs.py`
checks in CI that its frame layout matches `sim/sensor_inject.h`.

A main loop has this shape:

```c
#include "obsw_svf_protocol.h"

int main(void) {
    svf_init("0.1.0");  /* start-up banner, with your SRDB version */

    uint8_t frame[256];
    uint16_t frame_len;

    while (1) {
        uint8_t type = svf_read_frame(frame, sizeof(frame), &frame_len);

        if (type == SVF_FRAME_TC) {
            /* Dispatch the PUS telecommand (a space packet) */
            obsw_tc_dispatcher_feed(&dispatcher, frame, frame_len);
        }
        else if (type == SVF_FRAME_SENSOR) {
            /* Unpack the sensor frame and run the control step */
            svf_sensor_frame_t *s = (svf_sensor_frame_t *)frame;
            run_aocs(s);

            /* Send actuator commands back */
            svf_actuator_frame_t act = {0};
            act.mtq_dipole_x = my_mtq_cmd.x;
            act.sim_time     = s->sim_time;
            svf_write_actuator(&act);
        }

        /* Send this tick's TM, then the end-of-tick byte */
        drain_tm_store();
        svf_write_sync();
    }
}
```

The frame layouts are in the ICD:
[sensor frame](icd.md#sensor-frame-0x02) and
[actuator frame](icd.md#actuator-frame-0x03). OpenSVF checks the start-up
banner and the wire protocol version when it starts the binary.

---

## Renode socket mode

Under Renode, a target's UART is a TCP socket on port 3456, which OpenSVF
(or a test script) connects to. What works depends on the target:

| Target | Telecommands and TM | Housekeeping | Closed-loop AOCS |
|---|---|---|---|
| STM32H7 FreeRTOS | yes | yes | no: sensor frames are dropped (#54) |
| STM32H7 superloop | yes (S17, S20 only) | no | no |
| ZynqMP | yes (S8, S17) | no | no: stops answering in NOMINAL (#147) |

The full matrix, with the evidence for each cell, is on the
[Targets](targets.md) page.

**ZynqMP (aarch64 bare metal):**

```bash
cmake -S . -B build_zynqmp_baremetal \
    -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-none-elf.cmake \
    -DOBSW_BUILD_ZYNQMP=ON -DOBSW_BUILD_TESTS=OFF -DOBSW_BUILD_SIM=OFF
cmake --build build_zynqmp_baremetal
aarch64-none-elf-objcopy -O binary build_zynqmp_baremetal/obsw_zynqmp \
    build_zynqmp_baremetal/obsw_zynqmp.bin
renode renode/zynqmp_obsw.resc
```

**STM32H750 (Cortex-M7):**

```bash
# -DOBSW_FREERTOS=ON for the FreeRTOS image; leave it out for the superloop
cmake -S targets/stm32h7 -B build_stm32h7_renode \
    -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/stm32h7-toolchain.cmake \
    -DOBSW_ROOT=$(pwd) -DOBSW_RENODE=ON -DOBSW_FREERTOS=ON
cmake --build build_stm32h7_renode -j$(nproc)
renode renode/stm32h750_obsw.resc
```

For a headless run, use `renode --disable-xwt --plain <resc>` and keep its
stdin open; the socket is ready once Renode prints "GDB server with all CPUs
started". Do not probe the port with `echo > /dev/tcp/localhost/3456`: the
newline reaches the UART and is read as the start of a frame.

Renode runs the STM32H750 at about 0.4 times real time, and the FreeRTOS
image sends the TM of a TC after its end-of-tick byte. Both matter to a
harness with wall-clock timeouts; see [Known limits](known-limits.md).

---

## Troubleshooting

**No TM(17,2) in reply to a ping.** Check that the dispatcher is
initialised, that the TC is a space packet (not a transfer frame), and, on a
target UART, that your reader skips the start-up banner text before the
first frame. On STM32H7 FreeRTOS, read on past the first `0xFF`.

**The host sim exits with status 2.** It received a sensor frame that is not
50 bytes, typically from a wire protocol v3 sender (47 bytes).
