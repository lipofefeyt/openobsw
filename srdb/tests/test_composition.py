"""Tests for the generic additive SRDB composition boundary."""

from __future__ import annotations

from pathlib import Path

import pytest
import yaml

from obsw_srdb import (
    Event,
    HKSet,
    Parameter,
    SRDBComposer,
    SRDBCompositionError,
    SRDBContribution,
    SRDBContributionLoader,
    Telecommand,
)


def parameter(identifier: int = 0x6001, name: str = "external_voltage_mv") -> Parameter:
    return Parameter(
        id=identifier,
        name=name,
        description="External voltage",
        type="uint16",
        ptc=1,
        pfc=16,
        unit="mV",
    )


def event(identifier: int = 0x5001, name: str = "external_voltage_event") -> Event:
    return Event(
        id=identifier,
        name=name,
        severity="MEDIUM",
        description="External voltage event",
        safe_trigger=False,
        auxiliary_data=[],
    )


def hk_set(identifier: int = 5, name: str = "external_hk", parameter_name: str = "external_voltage_mv") -> HKSet:
    return HKSet(
        id=identifier,
        name=name,
        description="External housekeeping",
        parameters=[parameter_name],
        default_interval_ticks=0,
    )


def telecommand(
    name: str = "external_command",
    apid: int = 16,
    service: int = 99,
    subservice: int = 1,
) -> Telecommand:
    return Telecommand(
        name=name,
        description="External command",
        apid=apid,
        service=service,
        subservice=subservice,
        parameters=[],
    )


def write_contribution(root: Path) -> None:
    records = {
        "parameters": [parameter().model_dump(mode="json")],
        "telecommands": [telecommand().model_dump(mode="json")],
        "hk_sets": [hk_set().model_dump(mode="json")],
        "events": [event().model_dump(mode="json")],
    }
    for role, items in records.items():
        (root / f"{role}.yaml").write_text(
            yaml.safe_dump({role: items}, sort_keys=False),
            encoding="utf-8",
        )


def test_load_contribution_and_compose_without_mutating_base(srdb, tmp_path: Path) -> None:
    write_contribution(tmp_path)
    contribution = SRDBContributionLoader.load(tmp_path)

    base_counts = (
        len(srdb.parameters),
        len(srdb.telecommands),
        len(srdb.hk_sets),
        len(srdb.events),
    )
    composed = SRDBComposer.compose(srdb, [contribution])

    assert base_counts == (
        len(srdb.parameters),
        len(srdb.telecommands),
        len(srdb.hk_sets),
        len(srdb.events),
    )
    assert len(composed.parameters) == base_counts[0] + 1
    assert len(composed.telecommands) == base_counts[1] + 1
    assert len(composed.hk_sets) == base_counts[2] + 1
    assert len(composed.events) == base_counts[3] + 1
    assert composed.parameter_by_id(0x6001).name == "external_voltage_mv"
    assert composed.hk_set_by_id(5).parameters == ["external_voltage_mv"]
    assert composed.event_by_id(0x5001).name == "external_voltage_event"
    assert composed.spacecraft == srdb.spacecraft
    assert composed.spacecraft is not srdb.spacecraft


def test_empty_contribution_is_logical_noop(srdb) -> None:
    composed = SRDBComposer.compose(srdb, [SRDBContribution()])
    assert composed == srdb
    assert composed is not srdb


def test_parameter_id_collision_is_rejected(srdb) -> None:
    existing = srdb.parameters[0]
    contribution = SRDBContribution(
        parameters=(parameter(existing.id, "different_external_parameter"),)
    )
    with pytest.raises(SRDBCompositionError, match="Duplicate parameter ID"):
        SRDBComposer.compose(srdb, [contribution])


def test_parameter_name_collision_is_rejected(srdb) -> None:
    existing = srdb.parameters[0]
    contribution = SRDBContribution(parameters=(parameter(0x6001, existing.name),))
    with pytest.raises(SRDBCompositionError, match="Duplicate parameter name"):
        SRDBComposer.compose(srdb, [contribution])


def test_event_id_and_name_collisions_are_rejected(srdb) -> None:
    existing = srdb.events[0]
    with pytest.raises(SRDBCompositionError, match="Duplicate event ID"):
        SRDBComposer.compose(
            srdb,
            [SRDBContribution(events=(event(existing.id, "different_external_event"),))],
        )
    with pytest.raises(SRDBCompositionError, match="Duplicate event name"):
        SRDBComposer.compose(
            srdb,
            [SRDBContribution(events=(event(0x5001, existing.name),))],
        )


def test_hk_id_and_name_collisions_are_rejected(srdb) -> None:
    existing = srdb.hk_sets[0]
    with pytest.raises(SRDBCompositionError, match="Duplicate HK set ID"):
        SRDBComposer.compose(
            srdb,
            [SRDBContribution(hk_sets=(hk_set(existing.id, "different_external_hk", srdb.parameters[0].name),))],
        )
    with pytest.raises(SRDBCompositionError, match="Duplicate HK set name"):
        SRDBComposer.compose(
            srdb,
            [SRDBContribution(hk_sets=(hk_set(5, existing.name, srdb.parameters[0].name),))],
        )


def test_telecommand_tuple_and_name_collisions_are_rejected(srdb) -> None:
    existing = srdb.telecommands[0]
    with pytest.raises(SRDBCompositionError, match="Duplicate telecommand .* tuple"):
        SRDBComposer.compose(
            srdb,
            [
                SRDBContribution(
                    telecommands=(
                        telecommand(
                            "different_external_command",
                            existing.apid,
                            existing.service,
                            existing.subservice,
                        ),
                    )
                )
            ],
        )
    with pytest.raises(SRDBCompositionError, match="Duplicate telecommand name"):
        SRDBComposer.compose(
            srdb,
            [SRDBContribution(telecommands=(telecommand(existing.name),))],
        )


def test_unknown_hk_parameter_reference_is_rejected(srdb) -> None:
    contribution = SRDBContribution(
        hk_sets=(hk_set(parameter_name="does_not_exist"),)
    )
    with pytest.raises(SRDBCompositionError, match="references unknown parameter 'does_not_exist'"):
        SRDBComposer.compose(srdb, [contribution])


def test_contributed_hk_can_reference_contributed_parameter(srdb) -> None:
    contribution = SRDBContribution(
        parameters=(parameter(),),
        hk_sets=(hk_set(),),
    )
    composed = SRDBComposer.compose(srdb, [contribution])
    assert composed.hk_set_by_id(5).parameters == ["external_voltage_mv"]


def test_loader_requires_explicit_target_files(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError, match="parameters.yaml"):
        SRDBContributionLoader.load(tmp_path)


def test_loader_rejects_invalid_target_record(tmp_path: Path) -> None:
    for role in ("parameters", "telecommands", "hk_sets", "events"):
        items = []
        if role == "parameters":
            items = [{"id": 0x6001, "name": "bad", "description": "missing type"}]
        (tmp_path / f"{role}.yaml").write_text(
            yaml.safe_dump({role: items}, sort_keys=False),
            encoding="utf-8",
        )
    with pytest.raises(SRDBCompositionError, match="Invalid parameters contribution record"):
        SRDBContributionLoader.load(tmp_path)
