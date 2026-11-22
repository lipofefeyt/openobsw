# Traceability

Each requirement in the [SRS](SRS.md) with test method T has at least one
verifying test. `tools/trace.py` checks this in CI, together with two symbol
checks on the built library: no allocator and no writable globals.

Tests declare what they verify: a `/* @verifies OBSW-SRS-nnn */` comment
above a Unity test, or `@pytest.mark.requirement("OBSW-SRS-nnn")` on a Python
test. The Renode scripts under `renode/` are not scanned and do not run in CI;
the SRS cites them in Evidence lines.

--8<-- "docs/_gen/traceability.md"
