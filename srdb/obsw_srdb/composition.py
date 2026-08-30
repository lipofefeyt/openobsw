"""Additive composition support for externally contributed SRDB records.

The composition boundary is target-owned and generic. It knows nothing about
OrbitFabric or any other external producer: callers provide target-native
Parameter, Telecommand, HKSet and Event records, and receive a complete
validated SRDB while the base spacecraft record remains authoritative.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, TypeVar

import yaml
from pydantic import ValidationError

from .model import Event, HKSet, Parameter, SRDB, Telecommand


class SRDBCompositionError(ValueError):
    """Raised when an additive SRDB contribution cannot be composed safely."""


@dataclass(frozen=True)
class SRDBContribution:
    """Target-native additive records contributed to one complete base SRDB."""

    parameters: tuple[Parameter, ...] = ()
    telecommands: tuple[Telecommand, ...] = ()
    hk_sets: tuple[HKSet, ...] = ()
    events: tuple[Event, ...] = ()


_ModelT = TypeVar("_ModelT", Parameter, Telecommand, HKSet, Event)


class SRDBContributionLoader:
    """Load one additive contribution directory into typed target records."""

    FILES = {
        "parameters": ("parameters.yaml", Parameter),
        "telecommands": ("telecommands.yaml", Telecommand),
        "hk_sets": ("hk_sets.yaml", HKSet),
        "events": ("events.yaml", Event),
    }

    @classmethod
    def load(cls, data_dir: str | Path) -> SRDBContribution:
        root = Path(data_dir)
        loaded: dict[str, tuple[object, ...]] = {}
        for role, (filename, model_cls) in cls.FILES.items():
            loaded[role] = tuple(cls._load_records(root / filename, role, model_cls))
        return SRDBContribution(
            parameters=loaded["parameters"],  # type: ignore[arg-type]
            telecommands=loaded["telecommands"],  # type: ignore[arg-type]
            hk_sets=loaded["hk_sets"],  # type: ignore[arg-type]
            events=loaded["events"],  # type: ignore[arg-type]
        )

    @staticmethod
    def _load_records(path: Path, key: str, model_cls: type[_ModelT]) -> list[_ModelT]:
        if not path.is_file():
            raise FileNotFoundError(f"Required SRDB contribution file not found: {path}")
        try:
            payload = yaml.safe_load(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, yaml.YAMLError) as exc:
            raise SRDBCompositionError(f"Cannot read SRDB contribution file {path}: {exc}") from exc
        if not isinstance(payload, dict) or not isinstance(payload.get(key), list):
            raise SRDBCompositionError(
                f"SRDB contribution file {path} must contain a top-level '{key}' list"
            )
        try:
            return [model_cls(**record) for record in payload[key]]
        except (TypeError, ValidationError) as exc:
            raise SRDBCompositionError(
                f"Invalid {key} contribution record in {path}: {exc}"
            ) from exc


class SRDBComposer:
    """Compose additive target-native records into a complete validated SRDB."""

    @classmethod
    def compose(
        cls,
        base: SRDB,
        contributions: Iterable[SRDBContribution],
    ) -> SRDB:
        parameters = [item.model_copy(deep=True) for item in base.parameters]
        telecommands = [item.model_copy(deep=True) for item in base.telecommands]
        hk_sets = [item.model_copy(deep=True) for item in base.hk_sets]
        events = [item.model_copy(deep=True) for item in base.events]

        for contribution in contributions:
            parameters.extend(item.model_copy(deep=True) for item in contribution.parameters)
            telecommands.extend(item.model_copy(deep=True) for item in contribution.telecommands)
            hk_sets.extend(item.model_copy(deep=True) for item in contribution.hk_sets)
            events.extend(item.model_copy(deep=True) for item in contribution.events)

        cls._validate_unique(parameters, "id", "parameter ID")
        cls._validate_unique(parameters, "name", "parameter name")
        cls._validate_unique(events, "id", "event ID")
        cls._validate_unique(events, "name", "event name")
        cls._validate_unique(hk_sets, "id", "HK set ID")
        cls._validate_unique(hk_sets, "name", "HK set name")
        cls._validate_unique(telecommands, "name", "telecommand name")
        cls._validate_tc_tuples(telecommands)
        cls._validate_hk_references(parameters, hk_sets)

        return SRDB(
            spacecraft=base.spacecraft.model_copy(deep=True),
            parameters=parameters,
            telecommands=telecommands,
            hk_sets=hk_sets,
            events=events,
        )

    @staticmethod
    def _validate_unique(records: list[object], key: str, label: str) -> None:
        values = [getattr(record, key) for record in records]
        if len(values) != len(set(values)):
            raise SRDBCompositionError(f"Duplicate {label} detected during SRDB composition")

    @staticmethod
    def _validate_tc_tuples(records: list[Telecommand]) -> None:
        tuples = [(item.apid, item.service, item.subservice) for item in records]
        if len(tuples) != len(set(tuples)):
            raise SRDBCompositionError(
                "Duplicate telecommand (APID, service, subservice) tuple detected during SRDB composition"
            )

    @staticmethod
    def _validate_hk_references(parameters: list[Parameter], hk_sets: list[HKSet]) -> None:
        parameter_names = {item.name for item in parameters}
        missing: list[str] = []
        for hk_set in hk_sets:
            for parameter_name in hk_set.parameters:
                if parameter_name not in parameter_names:
                    missing.append(f"HK set '{hk_set.name}' references unknown parameter '{parameter_name}'")
        if missing:
            raise SRDBCompositionError(
                "SRDB composition cross-reference errors:\n"
                + "\n".join(f"  - {item}" for item in missing)
            )


class SRDBMaterializer:
    """Write a complete SRDB object as a normal loader-compatible data directory."""

    @classmethod
    def write(cls, srdb: SRDB, output_dir: str | Path) -> Path:
        root = Path(output_dir)
        if root.exists() and not root.is_dir():
            raise SRDBCompositionError(f"SRDB output path is not a directory: {root}")
        root.mkdir(parents=True, exist_ok=True)

        documents = {
            "spacecraft.yaml": {
                "spacecraft": srdb.spacecraft.model_dump(mode="json", exclude_none=True)
            },
            "parameters.yaml": {
                "parameters": [
                    item.model_dump(mode="json", exclude_none=True)
                    for item in srdb.parameters
                ]
            },
            "telecommands.yaml": {
                "telecommands": [
                    item.model_dump(mode="json", exclude_none=True)
                    for item in srdb.telecommands
                ]
            },
            "hk_sets.yaml": {
                "hk_sets": [
                    item.model_dump(mode="json", exclude_none=True)
                    for item in srdb.hk_sets
                ]
            },
            "events.yaml": {
                "events": [
                    item.model_dump(mode="json", exclude_none=True)
                    for item in srdb.events
                ]
            },
        }

        for filename, payload in documents.items():
            (root / filename).write_text(
                yaml.safe_dump(payload, sort_keys=False, allow_unicode=True),
                encoding="utf-8",
            )

        # Round-trip through the canonical target loader before declaring the
        # materialized directory valid. Import locally to keep loader ownership
        # separate and avoid a module-level dependency cycle.
        from .loader import SRDBLoader

        reloaded = SRDBLoader.load(root)
        if reloaded.model_dump(mode="json") != srdb.model_dump(mode="json"):
            raise SRDBCompositionError(
                "Materialized SRDB does not round-trip to the same logical target model"
            )
        return root
