# SRDB

The Spacecraft Resource Database (SRDB) is the source of truth for every
parameter, event, telecommand and housekeeping set. It lives as YAML in
[`srdb/data/`](https://github.com/lipofefeyt/openobsw/tree/main/srdb/data).
At build time, the `obsw-srdb` package turns it into
`build/include/obsw/srdb_generated.h` for the C code and
`build/xtce/mission.xtce` for ground tools such as YAMCS.

The pages in this section are generated from the same YAML:

- [Parameters](parameters.md): IDs, types, units, conversions, limits.
- [Events](events.md): S5 event IDs, severities, auxiliary data, SAFE triggers.
- [Telecommands](telecommands.md): service and subservice, arguments, the SAFE and STANDBY whitelist.
- [Housekeeping sets](hk-sets.md): S3 report layouts and default intervals.
