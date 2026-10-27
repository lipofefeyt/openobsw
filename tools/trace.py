#!/usr/bin/env python3
"""
Requirements traceability for openobsw.

Reads docs/SRS.md, collects the tests that verify each requirement, runs the
built-in library checks, writes a traceability matrix and fails if:

  - an IMPLEMENTED requirement with method T has no verifying test,
  - a test names a requirement that is not in the SRS,
  - a test verifies a SUPERSEDED requirement,
  - a built-in library check fails.

Tests declare what they verify:

  C (Unity):  a comment directly above the test function
                  /* @verifies OBSW-SRS-061, OBSW-SRS-081 */
                  void test_dispatch_no_route(void)
  Python:     @pytest.mark.requirement("OBSW-SRS-221")

Built-in checks (need --lib, the built libobsw.a):
  no_allocator         libobsw.a references no malloc/calloc/realloc/free  (OBSW-SRS-001)
  no_writable_globals  libobsw.a defines no writable data or BSS symbols   (OBSW-SRS-002)

Usage:
  tools/trace.py [--lib build/src/libobsw.a] [--out build/traceability.md]

Implements: OBSW-SRS-261
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SRS = REPO / "docs" / "SRS.md"
C_TEST_DIRS = [REPO / "test" / "unit", REPO / "test" / "integration"]
PY_TEST_DIRS = [REPO / "srdb" / "tests", REPO / "test" / "system"]

REQ_RE = re.compile(r"^\*\*(OBSW-SRS-\d{3})\*\* `\[(\w+)\]` `(\w+)` `([TAIR]+)`\s*$")
ID_RE = re.compile(r"OBSW-SRS-\d{3}")
C_TAG_RE = re.compile(r"/\*\s*@verifies\s+([^*]+?)\s*\*/")
C_FUNC_RE = re.compile(r"^\s*void\s+(test_\w+)\s*\(\s*void\s*\)")
PY_MARK_RE = re.compile(r"""pytest\.mark\.requirement\(\s*["'](OBSW-SRS-\d{3})["']\s*\)""")
PY_FUNC_RE = re.compile(r"^\s*def\s+(test_\w+)\s*\(")
PY_CLASS_RE = re.compile(r"^class\s+(\w+)")

STATUSES = {"DRAFT", "BASELINED", "IMPLEMENTED", "DEFERRED", "SUPERSEDED"}
ALLOCATORS = {"malloc", "calloc", "realloc", "free", "aligned_alloc"}


@dataclass
class Requirement:
    rid: str
    area: str
    status: str
    method: str
    text: str = ""
    tests: list[str] = field(default_factory=list)


def load_srs(path: Path) -> dict[str, Requirement]:
    reqs: dict[str, Requirement] = {}
    lines = path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines):
        m = REQ_RE.match(line)
        if not m:
            continue
        rid, area, status, method = m.groups()
        if rid in reqs:
            raise SystemExit(f"{path}:{i + 1}: duplicate requirement {rid}")
        if status not in STATUSES:
            raise SystemExit(f"{path}:{i + 1}: {rid} has unknown status {status}")
        text = lines[i + 1].strip() if i + 1 < len(lines) else ""
        reqs[rid] = Requirement(rid, area, status, method, text)
    if not reqs:
        raise SystemExit(f"{path}: no requirements found")
    return reqs


def collect_c(dirs: list[Path]) -> list[tuple[str, str, str]]:
    """(requirement id, test name, location) from Unity test files."""
    found = []
    for d in dirs:
        for f in sorted(d.glob("*.c")):
            pending: list[str] = []
            for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
                tag = C_TAG_RE.search(line)
                if tag:
                    pending += ID_RE.findall(tag.group(1))
                    continue
                fn = C_FUNC_RE.match(line)
                if fn and pending:
                    name = f"{f.stem}::{fn.group(1)}"
                    found += [(rid, name, f"{f.relative_to(REPO)}:{n}") for rid in pending]
                    pending = []
                elif line.strip() and pending:
                    raise SystemExit(f"{f.relative_to(REPO)}:{n}: @verifies tag not directly "
                                     "above a test function")
    return found


def collect_py(dirs: list[Path]) -> list[tuple[str, str, str]]:
    """Markers on a test, its class, or the module (pytestmark) all count."""
    found = []
    for d in dirs:
        for f in sorted(d.glob("test_*.py")):
            module_marks: list[str] = []
            class_marks: list[str] = []
            pending: list[str] = []
            cls = None
            for n, line in enumerate(f.read_text(encoding="utf-8").splitlines(), 1):
                marks = PY_MARK_RE.findall(line)
                if line.startswith("pytestmark"):
                    module_marks += marks
                    continue
                pending += marks
                c = PY_CLASS_RE.match(line)
                if c:
                    cls, class_marks, pending = c.group(1), pending, []
                    continue
                fn = PY_FUNC_RE.match(line)
                if fn:
                    method = line.startswith((" ", "\t")) and cls is not None
                    if not method:
                        cls, class_marks = None, []
                    owner = f"{cls}::" if method else ""
                    name = f"{f.stem}::{owner}{fn.group(1)}"
                    rids = dict.fromkeys(module_marks + class_marks + pending)
                    found += [(rid, name, f"{f.relative_to(REPO)}:{n}") for rid in rids]
                    pending = []
    return found


def nm_symbols(lib: Path) -> list[tuple[str, str]]:
    out = subprocess.run(["nm", str(lib)], capture_output=True, text=True, check=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 2 and len(parts[-2]) == 1:
            syms.append((parts[-2], parts[-1]))
    return syms


def lib_checks(lib: Path) -> list[tuple[str, str, bool, str]]:
    """(requirement, check name, passed, detail)."""
    syms = nm_symbols(lib)
    alloc = sorted({s for t, s in syms if t == "U" and s in ALLOCATORS})
    writable = sorted({s for t, s in syms if t in "bBdDgGsSC"})
    return [
        ("OBSW-SRS-001", "trace::no_allocator", not alloc,
         ", ".join(alloc) or "no allocator symbols"),
        ("OBSW-SRS-002", "trace::no_writable_globals", not writable,
         ", ".join(writable) or "no writable data or BSS symbols"),
    ]


def write_matrix(path: Path, reqs: dict[str, Requirement], untraced: list[str]) -> None:
    rows = ["# openobsw traceability matrix", "",
            "Generated by `tools/trace.py` from `docs/SRS.md` and the test sources.", "",
            "| Requirement | Area | Status | Method | Verified by |",
            "|---|---|---|---|---|"]
    for r in reqs.values():
        tests = "<br>".join(f"`{t}`" for t in sorted(set(r.tests)))
        if not tests:
            tests = "**no test**" if r.rid in untraced else "—"
        rows.append(f"| {r.rid} | {r.area} | {r.status} | {r.method} | {tests} |")
    impl = [r for r in reqs.values() if r.status == "IMPLEMENTED"]
    traced = [r for r in impl if r.tests]
    rows += ["", f"{len(reqs)} requirements; {len(impl)} implemented, "
             f"{len(traced)} of them with tests; {len(untraced)} untraced."]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--srs", type=Path, default=SRS)
    ap.add_argument("--lib", type=Path, help="built libobsw.a for the library checks")
    ap.add_argument("--out", type=Path, help="write the traceability matrix here")
    args = ap.parse_args(argv)

    reqs = load_srs(args.srs)
    errors: list[str] = []

    for rid, name, where in collect_c(C_TEST_DIRS) + collect_py(PY_TEST_DIRS):
        r = reqs.get(rid)
        if r is None:
            errors.append(f"{where}: {name} verifies unknown requirement {rid}")
        elif r.status == "SUPERSEDED":
            errors.append(f"{where}: {name} verifies superseded requirement {rid}")
        else:
            r.tests.append(name)

    if args.lib:
        for rid, name, ok, detail in lib_checks(args.lib):
            if ok:
                reqs[rid].tests.append(name)
            else:
                errors.append(f"{name} failed ({rid}): {detail}")

    untraced = [r.rid for r in reqs.values()
                if r.status == "IMPLEMENTED" and "T" in r.method and not r.tests
                and not (r.rid in ("OBSW-SRS-001", "OBSW-SRS-002") and not args.lib)]
    errors += [f"{rid} is IMPLEMENTED with method T but no test verifies it" for rid in untraced]

    if args.out:
        write_matrix(args.out, reqs, untraced)

    impl = sum(r.status == "IMPLEMENTED" for r in reqs.values())
    tests = {t for r in reqs.values() for t in r.tests}
    print(f"trace: {len(reqs)} requirements ({impl} implemented), "
          f"{len(tests)} verifying tests/checks")
    for e in errors:
        print(f"trace: error: {e}", file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
