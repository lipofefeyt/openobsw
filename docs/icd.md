# Wire protocol (ICD)

The wire protocol carries telecommands, telemetry and, in simulation, sensor
and actuator data between OpenOBSW and a ground tool or a validation facility
such as [OpenSVF](https://opensvf.readthedocs.io/). The host simulator speaks
it on stdin and stdout. The STM32H7 and ZynqMP targets speak it on their UART,
which Renode exposes as a TCP socket.

The tables on this page are generated from
[`sim/sensor_inject.h`](https://github.com/lipofefeyt/openobsw/blob/main/sim/sensor_inject.h),
the canonical definition. The MSP430 target uses a simpler framing, described
at the end.

## Framing

Every frame is a type byte, a 16-bit big-endian body length, then the body:

```
[type: 1 B][length: uint16, big-endian][body: length bytes]
```

The end-of-tick marker is the exception: a single `0xFF` byte with no length
and no body.

--8<-- "docs/_gen/wire-frame-types.md"

## Telecommand body (`0x01`)

The body is one CCSDS space packet (CCSDS 133.0-B) with a PUS-C TC secondary
header, starting at the primary header. It is not a TC transfer frame: no
target uses the transfer frame layer in `ccsds/tc_frame`.

- Every telecommand in the [SRDB](srdb/telecommands.md) is addressed to APID
  `0x010`, and the ground tools send `0x010`.
- The route tables use the wildcard APID `0xFFFF`, so a target accepts a
  telecommand on any APID.

## Telemetry body (`0x04`)

The body is one TM space packet with a PUS-C TM secondary header, from source
APID `0x103` (`apid_default` in `srdb/data/spacecraft.yaml`).

| Bytes | Content |
|---|---|
| 0 to 5 | CCSDS primary header |
| 6 to 16 | PUS-C TM secondary header; bytes 7 and 8 are service and subservice, bytes 9 and 10 the message counter |
| 13 to 16 | On-board time, whole seconds, big-endian. Zero on the bare-metal loops (OBSW-SRS-153) |
| 17 onwards | Application data |

## Sensor frame (`0x02`)

Sent to the OBSW once per simulation tick.

--8<-- "docs/_gen/wire-sensor-frame.md"

## Actuator frame (`0x03`)

Sent by the OBSW in reply to a sensor frame, on the targets that run attitude
control from sensor frames.

--8<-- "docs/_gen/wire-actuator-frame.md"

## Behaviour per target

The frame layout is the same on every target that speaks the protocol. What
each target does with the frames differs.

| | Host sim (x86_64, aarch64) | STM32H7 FreeRTOS | STM32H7 superloop | ZynqMP |
|---|---|---|---|---|
| Longest body read | 1024 B; a length of 0 or over 1024 ends the sim | 512 B | 512 B | 256 B |
| Sensor frame | Used, thermal fields included. Any length other than 50 B stops the sim with exit status 2 | Dropped | Only `sim_time` is used | Used if 50 B; thermal fields ignored |
| Actuator frame | One per sensor frame | Never sent | Never sent | One per 50 B sensor frame |
| `0xFF` | One per frame read, of any type | One per frame, including a dropped sensor frame and a bad length | One per frame; none for a bad length | One per frame read |
| TM caused by a TC | Before that frame's `0xFF` | Usually after it (see below) | Before that frame's `0xFF` | Before that frame's `0xFF` |
| Start-up text | Banner on stderr | Banner on the UART before the first frame | Banner on the UART | Banner on the UART |

On the host sim, a sensor frame is answered with all TM due in that tick, then
the actuator frame, then `0xFF`.

On STM32H7 FreeRTOS, the TMTC task queues a telecommand for the PUS task and
writes `0xFF` at once. The TM that the PUS task produces is sent while TMTC is
idle, so a reader that stops at `0xFF` gets it with the next tick. Nothing is
lost, but TM(1,x) of a telecommand arrives one or more ticks after it. The TC
queue holds 4 telecommands; when it is full, a new one is dropped without a
report.

A reader of a target UART should skip text before the first frame byte: the
start-up banner shares the UART with the frames.

## MSP430 framing

The MSP430 target does not speak this protocol. It reads
`[uint16 big-endian length][TC space packet]` and writes
`[uint16 big-endian length][TM space packet]`, with no type bytes and no
`0xFF` after each frame. Under Renode, a write to a sentinel address at the
end of each control cycle makes Renode emit `0xFF`.

## Copies of the frame layout

The layout is also written out in three other files that cannot include the
header:

- [`contrib/svf_protocol/obsw_svf_protocol.h`](https://github.com/lipofefeyt/openobsw/blob/main/contrib/svf_protocol/obsw_svf_protocol.h), the drop-in reference for other OBSW;
- [`renode/proto.py`](https://github.com/lipofefeyt/openobsw/blob/main/renode/proto.py), used by the Renode tests;
- [`sim/wire_proto.py`](https://github.com/lipofefeyt/openobsw/blob/main/sim/wire_proto.py), used by the sim harness scripts.

`tools/gen_docs.py` fails the CI docs job when any of them differs from
`sim/sensor_inject.h` in field order, field types, frame type bytes or
protocol version.
