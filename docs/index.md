# OpenOBSW

OpenOBSW is a small, permissively licensed PUS-C on-board software stack in
portable C11, with no dynamic allocation and no global state in the core. It
is flight-oriented, not flight-proven: it has not flown.

Its validation facility is [OpenSVF](https://opensvf.readthedocs.io/), which
runs the host simulator closed loop against a physics engine.

## Where to start

- **Getting started**: set up the development environment and build a target.
- **Concepts**: how the software is put together: layers, modes and FDIR,
  the FreeRTOS tasks.
- **Reference**: the requirements (SRS) and their tests, and the
  mission-specific configuration.
- **Known limits**: what each release does not do.
- **Releases**: the changelog.

The [README](https://github.com/lipofefeyt/openobsw#readme) lists the targets
and the roadmap.
