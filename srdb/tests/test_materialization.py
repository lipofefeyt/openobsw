"""Tests for complete SRDB materialization."""

from __future__ import annotations

from pathlib import Path

from obsw_srdb import (
    HKSet,
    Parameter,
    SRDBComposer,
    SRDBContribution,
    SRDBLoader,
    SRDBMaterializer,
)


def test_materialized_complete_srdb_round_trips(srdb, tmp_path: Path) -> None:
    contribution = SRDBContribution(
        parameters=(
            Parameter(
                id=0x6001,
                name="external_voltage_mv",
                description="External voltage",
                type="uint16",
                ptc=1,
                pfc=16,
                unit="mV",
            ),
        ),
        hk_sets=(
            HKSet(
                id=5,
                name="external_hk",
                description="External housekeeping",
                parameters=["external_voltage_mv"],
                default_interval_ticks=0,
            ),
        ),
    )
    composed = SRDBComposer.compose(srdb, [contribution])

    output = SRDBMaterializer.write(composed, tmp_path / "assembled")
    reloaded = SRDBLoader.load(output)

    assert reloaded == composed
    assert reloaded.parameter_by_id(0x6001).name == "external_voltage_mv"
    assert reloaded.hk_set_by_id(5).parameters == ["external_voltage_mv"]


def test_materialization_writes_only_complete_srdb_contract_files(srdb, tmp_path: Path) -> None:
    output = SRDBMaterializer.write(srdb, tmp_path / "assembled")
    assert {path.name for path in output.iterdir()} == {
        "spacecraft.yaml",
        "parameters.yaml",
        "telecommands.yaml",
        "hk_sets.yaml",
        "events.yaml",
    }


def test_materialization_is_byte_stable_for_same_model(srdb, tmp_path: Path) -> None:
    first = SRDBMaterializer.write(srdb, tmp_path / "first")
    second = SRDBMaterializer.write(srdb, tmp_path / "second")

    for filename in SRDBLoader.REQUIRED_FILES:
        assert (first / filename).read_bytes() == (second / filename).read_bytes()
