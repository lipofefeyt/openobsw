"""
loader.py — YAML loader and cross-reference validator for the openobsw SRDB.

Usage:
    from obsw_srdb import SRDBLoader
    srdb = SRDBLoader.load(data_dir="path/to/srdb/data")
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Union

import yaml

from .model import (
    Event,
    HKSet,
    Parameter,
    SRDB,
    Spacecraft,
    Telecommand,
)


class SRDBValidationError(Exception):
    """Raised when SRDB cross-reference validation fails."""


class SRDBLoader:
    """Loads and validates the SRDB from a directory of YAML files."""

    REQUIRED_FILES = (
        "spacecraft.yaml",
        "parameters.yaml",
        "telecommands.yaml",
        "hk_sets.yaml",
        "events.yaml",
    )

    @classmethod
    def load(cls, data_dir: Union[str, Path]) -> SRDB:
        """
        Load all SRDB YAML files from *data_dir* and return a validated SRDB.

        Raises:
            FileNotFoundError   if a required file is missing
            pydantic.ValidationError  if any field fails type/range validation
            SRDBValidationError if cross-reference validation fails
        """
        data_dir = Path(data_dir)
        cls._check_files(data_dir)

        spacecraft  = cls._load_spacecraft(data_dir / "spacecraft.yaml")
        parameters  = cls._load_list(data_dir / "parameters.yaml",
                                      "parameters", Parameter)
        telecommands = cls._load_list(data_dir / "telecommands.yaml",
                                       "telecommands", Telecommand)
        hk_sets     = cls._load_list(data_dir / "hk_sets.yaml",
                                      "hk_sets", HKSet)
        events      = cls._load_list(data_dir / "events.yaml",
                                      "events", Event)

        srdb = SRDB(
            spacecraft=spacecraft,
            parameters=parameters,
            telecommands=telecommands,
            hk_sets=hk_sets,
            events=events,
        )

        cls._validate_cross_references(srdb)
        cls._validate_unique_ids(srdb)
        return srdb

    # -----------------------------------------------------------------------
    # Private helpers
    # -----------------------------------------------------------------------

    @classmethod
    def _check_files(cls, data_dir: Path) -> None:
        for fname in cls.REQUIRED_FILES:
            path = data_dir / fname
            if not path.exists():
                raise FileNotFoundError(
                    f"Required SRDB file not found: {path}"
                )

    @classmethod
    def _load_yaml(cls, path: Path) -> dict:
        with open(path, encoding="utf-8") as f:
            return yaml.safe_load(f)

    @classmethod
    def _load_spacecraft(cls, path: Path) -> Spacecraft:
        data = cls._load_yaml(path)
        return Spacecraft(**data["spacecraft"])

    @classmethod
    def _load_list(cls, path: Path, key: str, model_cls) -> list:
        data = cls._load_yaml(path)
        return [model_cls(**item) for item in data[key]]

    @classmethod
    def _validate_cross_references(cls, srdb: SRDB) -> None:
        """Validate that all names referenced in HK sets exist in parameters."""
        param_names = {p.name for p in srdb.parameters}
        errors = []

        for hk in srdb.hk_sets:
            for pname in hk.parameters:
                if pname not in param_names:
                    errors.append(
                        f"HK set '{hk.name}' references unknown parameter '{pname}'"
                    )

        if errors:
            raise SRDBValidationError(
                "SRDB cross-reference errors:\n" + "\n".join(f"  - {e}" for e in errors)
            )

    @classmethod
    def _validate_unique_ids(cls, srdb: SRDB) -> None:
        """Validate that all IDs within each entity type are unique."""
        errors = []

        param_ids = [p.id for p in srdb.parameters]
        if len(param_ids) != len(set(param_ids)):
            errors.append("Duplicate parameter IDs detected")

        event_ids = [e.id for e in srdb.events]
        if len(event_ids) != len(set(event_ids)):
            errors.append("Duplicate event IDs detected")

        hk_ids = [h.id for h in srdb.hk_sets]
        if len(hk_ids) != len(set(hk_ids)):
            errors.append("Duplicate HK set IDs detected")

        tc_keys = [(t.apid, t.service, t.subservice) for t in srdb.telecommands]
        if len(tc_keys) != len(set(tc_keys)):
            errors.append("Duplicate telecommand (APID, svc, subsvc) tuples detected")

        if errors:
            raise SRDBValidationError(
                "SRDB uniqueness errors:\n" + "\n".join(f"  - {e}" for e in errors)
            )