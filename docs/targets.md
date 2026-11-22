# Targets

OpenOBSW builds for six targets. They share the core library and differ in
what their main loop wires up. This page is generated from
[`docs/targets.yaml`](https://github.com/lipofefeyt/openobsw/blob/main/docs/targets.yaml);
CI checks that file against the route table of each target and that every
test it cites exists.

## Support tiers

--8<-- "docs/_gen/targets-summary.md"

"In CI on every PR" means: the host and aarch64 simulators are built and run
the unit, integration and system tests; the STM32H7 FreeRTOS image is built,
and its behaviour is tested in Renode, locally.

## Features

Each cell says whether the target does it and what shows it:

- **CI**: a test that runs in this repository's CI;
- **Renode, local**: a script under `renode/` run by hand against Renode;
- **bench**: run by hand on the board, with no test script;
- no evidence kind: the code does it, nothing tests it.

--8<-- "docs/_gen/targets-features.md"

## Routed telecommands

--8<-- "docs/_gen/targets-tcs.md"
