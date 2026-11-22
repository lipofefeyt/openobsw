#!/usr/bin/env python3
"""
Generate the reference tables of the openobsw documentation site (#136).

Writes Markdown fragments into docs/_gen/ (gitignored); the pages in docs/
include them with pymdownx.snippets. Run it before `mkdocs build`; CI and
Read the Docs both do.

Sources, each the single owner of its facts:

  sim/sensor_inject.h        wire protocol v4: frame types, sensor and actuator frames
  srdb/data/*.yaml           parameters, events, telecommands, HK sets
  docs/SRS.md + test files   requirements and the tests that verify them (tools/trace.py)
  docs/targets.yaml          per-target support, checked against the route tables

Checks (exit status 1 on any failure):

  - the three other copies of the frame layout agree with sim/sensor_inject.h:
    contrib/svf_protocol/obsw_svf_protocol.h, renode/proto.py, sim/wire_proto.py
  - each target's `tcs` in docs/targets.yaml matches the route table in its source
  - every test cited in docs/targets.yaml exists

Usage:
  tools/gen_docs.py [--out docs/_gen] [--check]   (--check: run the checks, write nothing)
"""

from __future__ import annotations

import argparse
import ast
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "srdb"))
sys.path.insert(0, str(REPO / "tools"))

from obsw_srdb import SRDBLoader  # noqa: E402

import trace as srs_trace  # noqa: E402  (tools/trace.py)

GH = "https://github.com/lipofefeyt/openobsw/blob/main"

# --------------------------------------------------------------------------
# Wire protocol
# --------------------------------------------------------------------------

C_TYPES = {
    "float": ("f", "float32"),
    "uint8_t": ("B", "uint8"),
    "int8_t": ("b", "int8"),
    "uint16_t": ("H", "uint16"),
    "int16_t": ("h", "int16"),
    "uint32_t": ("I", "uint32"),
    "int32_t": ("i", "int32"),
}

STRUCT_RE = re.compile(r"typedef\s+struct\s*\{(.*?)\}\s*(\w+)\s*;", re.S)
FIELD_RE = re.compile(r"^\s*(\w+)\s+(\w+)\s*;\s*(?:/\*\*<\s*(.*?)\s*\*/)?")
DEFINE_RE = re.compile(r"^#define\s+(\w+)\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)U?\s*\)?", re.M)


@dataclass
class Field:
    name: str
    ctype: str
    comment: str

    @property
    def code(self) -> str:
        return C_TYPES[self.ctype][0]


def c_structs(path: Path) -> dict[str, list[Field]]:
    text = path.read_text(encoding="utf-8")
    out: dict[str, list[Field]] = {}
    for body, name in STRUCT_RE.findall(text):
        body = re.sub(r"/\*(?!\*<).*?\*/", "", body, flags=re.S)  # drop block comments
        fields = []
        for line in body.splitlines():
            m = FIELD_RE.match(line)
            if m:
                ctype, fname, comment = m.groups()
                if ctype not in C_TYPES:
                    raise SystemExit(f"{path}: {name}.{fname}: unknown type {ctype}")
                fields.append(Field(fname, ctype, comment or ""))
        out[name] = fields
    return out


def c_defines(path: Path) -> dict[str, int]:
    return {k: int(v, 0) for k, v in DEFINE_RE.findall(path.read_text(encoding="utf-8"))}


def py_constants(path: Path) -> dict[str, object]:
    tree = ast.parse(path.read_text(encoding="utf-8"))
    out: dict[str, object] = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Constant):
            for t in node.targets:
                if isinstance(t, ast.Name):
                    out[t.id] = node.value.value
    return out


def fmt(fields: list[Field]) -> str:
    return "<" + "".join(f.code for f in fields)


CANON = REPO / "sim/sensor_inject.h"


def wire_model():
    structs = c_structs(CANON)
    defs = c_defines(CANON)
    return structs["obsw_sensor_frame_t"], structs["obsw_actuator_frame_t"], defs


def check_wire() -> list[str]:
    sensor, actuator, defs = wire_model()
    errors: list[str] = []
    want = {
        "sensor": fmt(sensor), "actuator": fmt(actuator),
        "TC": defs["OBSW_FRAME_TC"], "SENSOR": defs["OBSW_FRAME_SENSOR"],
        "ACTUATOR": defs["OBSW_FRAME_ACTUATOR"], "TM": defs["OBSW_FRAME_TM"],
        "version": defs["OBSW_WIRE_PROTOCOL_VERSION"],
    }

    def cmp(where: str, what: str, got: object) -> None:
        if got != want[what]:
            errors.append(f"{where}: {what} is {got!r}, sim/sensor_inject.h has {want[what]!r}")

    # contrib/svf_protocol: same fields by name and type
    contrib = REPO / "contrib/svf_protocol/obsw_svf_protocol.h"
    cs, cd = c_structs(contrib), c_defines(contrib)
    for canon, copy, label in ((sensor, cs.get("svf_sensor_frame_t", []), "svf_sensor_frame_t"),
                               (actuator, cs.get("svf_actuator_frame_t", []), "svf_actuator_frame_t")):
        a = [(f.name, f.ctype) for f in canon]
        b = [(f.name, f.ctype) for f in copy]
        if a != b:
            errors.append(f"{contrib.relative_to(REPO)}: {label} fields {b} differ from {a}")
    for key, name in (("TC", "SVF_FRAME_TC"), ("SENSOR", "SVF_FRAME_SENSOR"),
                      ("ACTUATOR", "SVF_FRAME_ACTUATOR"), ("TM", "SVF_FRAME_TM"),
                      ("version", "SVF_PROTOCOL_VERSION")):
        cmp(f"{contrib.relative_to(REPO)} {name}", key, cd.get(name))
    if cd.get("SVF_SYNC_BYTE") != 0xFF:
        errors.append(f"{contrib.relative_to(REPO)}: SVF_SYNC_BYTE is not 0xFF")

    # Python copies: struct format strings and frame type bytes
    for rel, sfmt, afmt in (("renode/proto.py", "_SENSOR_FMT", "_ACTUAT_FMT"),
                            ("sim/wire_proto.py", "SENSOR_FMT", "ACTUAT_FMT")):
        c = py_constants(REPO / rel)
        cmp(f"{rel} {sfmt}", "sensor", c.get(sfmt))
        cmp(f"{rel} {afmt}", "actuator", c.get(afmt))
        for key, name in (("TC", "FRAME_TC"), ("SENSOR", "FRAME_SENSOR"),
                          ("ACTUATOR", "FRAME_ACT"), ("TM", "FRAME_TM")):
            cmp(f"{rel} {name}", key, c.get(name))
        if "SENSOR_LEN" in c and c["SENSOR_LEN"] != struct.calcsize(want["sensor"]):
            errors.append(f"{rel}: SENSOR_LEN is {c['SENSOR_LEN']}, the frame is "
                          f"{struct.calcsize(want['sensor'])} B")
    return errors


def frame_table(fields: list[Field]) -> str:
    rows = ["| Offset | Size | Type | Field | Meaning |", "|---:|---:|---|---|---|"]
    off = 0
    for f in fields:
        size = struct.calcsize("<" + f.code)
        rows.append(f"| {off} | {size} | {C_TYPES[f.ctype][1]} | `{f.name}` | {f.comment} |")
        off += size
    rows.append(f"| | **{off}** | | | total, packed, little-endian |")
    return "\n".join(rows) + "\n"


def gen_wire(out: Path) -> None:
    sensor, actuator, defs = wire_model()
    src = f"[`sim/sensor_inject.h`]({GH}/sim/sensor_inject.h)"
    types = [
        "| Type | Direction | Body | Name in the header |",
        "|---|---|---|---|",
        f"| `0x{defs['OBSW_FRAME_TC']:02X}` | to OBSW | TC space packet (CCSDS primary header first) | `OBSW_FRAME_TC` |",
        f"| `0x{defs['OBSW_FRAME_SENSOR']:02X}` | to OBSW | sensor frame, {struct.calcsize(fmt(sensor))} B | `OBSW_FRAME_SENSOR` |",
        f"| `0x{defs['OBSW_FRAME_ACTUATOR']:02X}` | from OBSW | actuator frame, {struct.calcsize(fmt(actuator))} B | `OBSW_FRAME_ACTUATOR` |",
        f"| `0x{defs['OBSW_FRAME_TM']:02X}` | from OBSW | TM space packet | `OBSW_FRAME_TM` |",
        "| `0xFF` | from OBSW | none: a single byte, end of tick | (literal) |",
    ]
    (out / "wire-frame-types.md").write_text(
        f"Wire protocol version **{defs['OBSW_WIRE_PROTOCOL_VERSION']}** "
        f"(`OBSW_WIRE_PROTOCOL_VERSION` in {src}).\n\n" + "\n".join(types) + "\n",
        encoding="utf-8")
    (out / "wire-sensor-frame.md").write_text(
        f"`obsw_sensor_frame_t` in {src}:\n\n" + frame_table(sensor), encoding="utf-8")
    (out / "wire-actuator-frame.md").write_text(
        f"`obsw_actuator_frame_t` in {src}:\n\n" + frame_table(actuator), encoding="utf-8")


# --------------------------------------------------------------------------
# SRDB
# --------------------------------------------------------------------------

def cell(s: object) -> str:
    return str(s).replace("|", "\\|").replace("\n", " ")


def num(v: float | None) -> str:
    return "" if v is None else f"{v:g}"


def gen_srdb(out: Path) -> None:
    db = SRDBLoader.load(REPO / "srdb/data")

    def src(name: str) -> str:
        return f"[`srdb/data/{name}`]({GH}/srdb/data/{name})"

    rows = [f"Source: {src('parameters.yaml')}, {len(db.parameters)} parameters.", "",
            "| ID | Name | Type | Unit | Raw to engineering | Limits soft / hard | Subsystem | Description |",
            "|---|---|---|---|---|---|---|---|"]
    for p in sorted(db.parameters, key=lambda p: p.id):
        conv = (f"{p.conversion.slope:g} * raw + {p.conversion.offset:g}"
                if p.conversion else "")
        lim = ""
        if p.limits:
            soft = f"{num(p.limits.soft_low)} .. {num(p.limits.soft_high)}"
            hard = f"{num(p.limits.hard_low)} .. {num(p.limits.hard_high)}"
            lim = f"{soft} / {hard}"
        desc = cell(p.description)
        if p.enumeration:
            desc += "<br>" + ", ".join(f"{e.value} = `{e.label}`" for e in p.enumeration)
        rows.append(f"| `0x{p.id:04X}` | `{p.name}` | {p.type.value} | {p.unit or ''} | {conv} "
                    f"| {lim} | {p.subsystem or ''} | {desc} |")
    (out / "srdb-parameters.md").write_text("\n".join(rows) + "\n", encoding="utf-8")

    rows = [f"Source: {src('events.yaml')}, {len(db.events)} events. Severity maps to "
            "TM(5,1) INFO, (5,2) LOW, (5,3) MEDIUM, (5,4) HIGH.", "",
            "| ID | Name | Severity | Commands SAFE | Auxiliary data | Description |",
            "|---|---|---|---|---|---|"]
    for e in sorted(db.events, key=lambda e: e.id):
        aux = "<br>".join(f"`{a.name}` ({a.type.value})" for a in e.auxiliary_data)
        rows.append(f"| `0x{e.id:04X}` | `{e.name}` | {e.severity.value} "
                    f"| {'yes' if e.safe_trigger else ''} | {aux} | {cell(e.description)} |")
    (out / "srdb-events.md").write_text("\n".join(rows) + "\n", encoding="utf-8")

    rows = [f"Source: {src('telecommands.yaml')}, {len(db.telecommands)} telecommands. "
            "Arguments are big-endian, in this order.", "",
            "| TC | Name | APID | Arguments | Allowed in SAFE and STANDBY | Description |",
            "|---|---|---|---|---|---|"]
    for t in sorted(db.telecommands, key=lambda t: (t.service, t.subservice)):
        args = "<br>".join(
            f"`{a.name}` ({a.type.value}{', repeated ' + a.count_from + ' times' if a.count_from else ''})"
            for a in t.parameters)
        rows.append(f"| TC({t.service},{t.subservice}) | `{t.name}` | `0x{t.apid:03X}` | {args} "
                    f"| {'yes' if t.safe_allowed else ''} | {cell(t.description)} |")
    (out / "srdb-telecommands.md").write_text("\n".join(rows) + "\n", encoding="utf-8")

    rows = [f"Source: {src('hk_sets.yaml')}, {len(db.hk_sets)} sets. Report: TM(3,25), "
            "parameters big-endian in this order.", "",
            "| SID | Name | Default interval (ticks) | SPID | Parameters | Description |",
            "|---|---|---|---|---|---|"]
    for s in sorted(db.hk_sets, key=lambda s: s.id):
        params = "<br>".join(
            f"`{n}` ({db.parameter_by_name(n).type.value})" for n in s.parameters)
        rows.append(f"| {s.id} | `{s.name}` | {s.default_interval_ticks or 'off'} "
                    f"| {s.spid or ''} | {params} | {cell(s.description)} |")
    (out / "srdb-hk-sets.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------
# Requirements traceability
# --------------------------------------------------------------------------

def gen_trace(out: Path) -> None:
    reqs = srs_trace.load_srs(srs_trace.SRS)
    for rid, name, _ in (srs_trace.collect_c(srs_trace.C_TEST_DIRS)
                         + srs_trace.collect_py(srs_trace.PY_TEST_DIRS)):
        if rid in reqs:
            reqs[rid].tests.append(name)
    lib = {"OBSW-SRS-001": "trace::no_allocator", "OBSW-SRS-002": "trace::no_writable_globals"}
    rows = ["| Requirement | Area | Status | Method | Verified by |", "|---|---|---|---|---|"]
    for r in reqs.values():
        tests = sorted(set(r.tests))
        if r.rid in lib:
            tests.append(f"{lib[r.rid]} (symbol check on `libobsw.a` in CI)")
        shown = "<br>".join(f"`{t}`" if "::" in t and "(" not in t else t for t in tests)
        rows.append(f"| {r.rid} | {r.area} | {r.status} | {r.method} | {shown} |")
    impl = [r for r in reqs.values() if r.status == "IMPLEMENTED"]
    rows += ["", f"{len(reqs)} requirements, {len(impl)} implemented."]
    (out / "traceability.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------
# Targets
# --------------------------------------------------------------------------

STATUS = {"supported": "Yes", "partial": "Partial", "no": "No", "unknown": "Not shown"}
EVIDENCE = {"ci": "CI", "renode": "Renode, local", "bench": "bench", "none": "none"}
ROUTE_RE = re.compile(r"obsw_tc_route_t\s+\w+\s*\[[^\]]*\]\s*=\s*\{(.*?)\n\s*\};", re.S)
ENTRY_RE = re.compile(r"\.service\s*=\s*(\w+)\s*,\s*\.subservice\s*=\s*(\w+)")


def route_table(path: Path, symbols: dict[str, int]) -> set[str]:
    m = ROUTE_RE.search(path.read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"{path}: no obsw_tc_route_t table found")
    out = set()
    for svc, sub in ENTRY_RE.findall(m.group(1)):
        vals = [int(v, 0) if v[0].isdigit() else symbols[v] for v in (svc, sub)]
        out.add(f"{vals[0]}/{vals[1]}")
    return out


def tc_symbols() -> dict[str, int]:
    db = SRDBLoader.load(REPO / "srdb/data")
    sym = {}
    for t in db.telecommands:
        sym[f"SRDB_TC_{t.name.upper()}_SVC"] = t.service
        sym[f"SRDB_TC_{t.name.upper()}_SUBSVC"] = t.subservice
    return sym


def test_exists(ref: str) -> bool:
    path, _, func = ref.partition("::")
    f = REPO / path
    if not f.is_file():
        return False
    return not func or re.search(rf"^\s*(def|void)\s+{re.escape(func)}\s*\(",
                                 f.read_text(encoding="utf-8"), re.M) is not None


def load_targets() -> dict:
    return yaml.safe_load((REPO / "docs/targets.yaml").read_text(encoding="utf-8"))


def check_targets() -> list[str]:
    doc = load_targets()
    errors: list[str] = []
    sym = tc_symbols()
    ids = [t["id"] for t in doc["targets"]]
    for t in doc["targets"]:
        if t["tier"] not in doc["tiers"]:
            errors.append(f"targets.yaml: {t['id']}: unknown tier {t['tier']}")
        routed = route_table(REPO / t["routes"], sym)
        listed = set(t["tcs"])
        if routed != listed:
            errors.append(f"targets.yaml: {t['id']}: tcs differ from {t['routes']}: "
                          f"only in YAML {sorted(listed - routed)}, "
                          f"only in code {sorted(routed - listed)}")
    for feat in doc["features"]:
        if set(feat["cells"]) != set(ids):
            errors.append(f"targets.yaml: '{feat['name']}': cells for {sorted(feat['cells'])}, "
                          f"targets are {sorted(ids)}")
        for tid, c in feat["cells"].items():
            where = f"targets.yaml: '{feat['name']}' / {tid}"
            if c.get("status") not in STATUS:
                errors.append(f"{where}: unknown status {c.get('status')!r}")
            for ev in c.get("evidence", []):
                if ev not in EVIDENCE:
                    errors.append(f"{where}: unknown evidence {ev!r}")
            if c.get("ref") and not test_exists(c["ref"]):
                errors.append(f"{where}: cited test {c['ref']} not found")
    return errors


def gen_targets(out: Path) -> None:
    doc = load_targets()
    db = SRDBLoader.load(REPO / "srdb/data")
    names = {f"{t.service}/{t.subservice}": t.name for t in db.telecommands}
    targets = doc["targets"]

    rows = ["| Target | Build | Tier | Transport | Route table |", "|---|---|---|---|---|"]
    for t in targets:
        rows.append(f"| {t['short']} | {t['name']} | {t['tier']}: {doc['tiers'][t['tier']]} | {t['transport']} "
                    f"| [`{t['routes']}`]({GH}/{t['routes']}) |")
    (out / "targets-summary.md").write_text("\n".join(rows) + "\n", encoding="utf-8")

    head = "| Feature | " + " | ".join(t["short"] for t in targets) + " |"
    rows = [head, "|---" * (len(targets) + 1) + "|"]
    notes = []
    for feat in doc["features"]:
        cells = []
        for t in targets:
            c = feat["cells"][t["id"]]
            ev = ", ".join(EVIDENCE[e] for e in c.get("evidence", []) if e != "none")
            cells.append(f"{STATUS[c['status']]}" + (f" ({ev})" if ev else ""))
        rows.append(f"| {feat['name']} | " + " | ".join(cells) + " |")
        lines = []
        for t in targets:
            c = feat["cells"][t["id"]]
            bits = []
            if c.get("ref"):
                bits.append(f"[`{c['ref']}`]({GH}/{c['ref'].split('::')[0]})")
            if c.get("note"):
                bits.append(c["note"])
            if bits:
                lines.append(f"- {t['short']}: " + " ".join(bits))
        if lines:
            notes.append(f"**{feat['name']}**\n\n" + "\n".join(lines))
    (out / "targets-features.md").write_text(
        "\n".join(rows) + "\n\n" + "\n\n".join(notes) + "\n", encoding="utf-8")

    all_tcs = sorted({tc for t in targets for tc in t["tcs"]},
                     key=lambda s: tuple(int(x) for x in s.split("/")))
    rows = ["| TC | SRDB name | " + " | ".join(t["short"] for t in targets) + " |",
            "|---|---" + "|---" * len(targets) + "|"]
    for tc in all_tcs:
        svc, sub = tc.split("/")
        marks = " | ".join("yes" if tc in t["tcs"] else "" for t in targets)
        rows.append(f"| TC({svc},{sub}) | `{names.get(tc, '')}` | {marks} |")
    (out / "targets-tcs.md").write_text("\n".join(rows) + "\n", encoding="utf-8")


# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1].strip())
    ap.add_argument("--out", type=Path, default=REPO / "docs/_gen")
    ap.add_argument("--check", action="store_true", help="run the checks only")
    args = ap.parse_args(argv)

    errors = check_wire() + check_targets()
    for e in errors:
        print(f"gen_docs: error: {e}", file=sys.stderr)
    if errors:
        return 1
    if not args.check:
        args.out.mkdir(parents=True, exist_ok=True)
        gen_wire(args.out)
        gen_srdb(args.out)
        gen_trace(args.out)
        gen_targets(args.out)
        print(f"gen_docs: wrote {len(list(args.out.glob('*.md')))} files to {args.out}")
    print("gen_docs: wire protocol copies and targets.yaml agree with their sources")
    return 0


if __name__ == "__main__":
    sys.exit(main())
