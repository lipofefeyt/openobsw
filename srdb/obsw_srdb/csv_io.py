"""
csv_io.py — CSV import/export for the openobsw SRDB.

Provides round-trip CSV serialisation for engineering workflows:
  - Export SRDB to CSV for review in Excel/Google Sheets
  - Import CSV back to validate against YAML baseline

Usage:
    from obsw_srdb.csv_io import export_csv, import_csv
    export_csv(srdb, output_dir="srdb/export/")
    srdb = import_csv(data_dir="srdb/export/")
"""
from __future__ import annotations

import csv
import io
from pathlib import Path
from typing import Union

from .model import (
    Event,
    HKSet,
    Parameter,
    ParameterConversion,
    ParameterLimits,
    SRDB,
    Spacecraft,
    Severity,
    Telecommand,
    TCParameter,
    ConversionType,
    ParameterType,
)


# ------------------------------------------------------------------ #
# Export                                                              #
# ------------------------------------------------------------------ #

def export_csv(srdb: SRDB, output_dir: Union[str, Path]) -> None:
    """Export SRDB to CSV files in output_dir."""
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    _export_parameters(srdb, out / "parameters.csv")
    _export_telecommands(srdb, out / "telecommands.csv")
    _export_events(srdb, out / "events.csv")
    _export_hk_sets(srdb, out / "hk_sets.csv")


def _export_parameters(srdb: SRDB, path: Path) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "id_hex", "name", "description", "type", "unit",
            "conv_type", "conv_slope", "conv_offset",
            "limit_soft_low", "limit_soft_high",
            "limit_hard_low", "limit_hard_high",
        ])
        for p in srdb.parameters:
            c = p.conversion
            lim = p.limits
            w.writerow([
                f"0x{p.id:04X}", p.name, p.description, p.type.value,
                p.unit or "",
                c.type.value if c else "",
                c.slope if c else "",
                c.offset if c else "",
                lim.soft_low if lim else "",
                lim.soft_high if lim else "",
                lim.hard_low if lim else "",
                lim.hard_high if lim else "",
            ])


def _export_telecommands(srdb: SRDB, path: Path) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "name", "description", "apid_hex",
            "service", "subservice", "parameters",
        ])
        for tc in srdb.telecommands:
            params_str = ";".join(
                f"{p.name}:{p.type.value}" for p in tc.parameters
            )
            w.writerow([
                tc.name, tc.description, f"0x{tc.apid:03X}",
                tc.service, tc.subservice, params_str,
            ])


def _export_events(srdb: SRDB, path: Path) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "id_hex", "name", "severity", "description",
            "safe_trigger", "auxiliary_data",
        ])
        for e in srdb.events:
            aux_str = ";".join(
                f"{a.name}:{a.type.value}" for a in e.auxiliary_data
            )
            w.writerow([
                f"0x{e.id:04X}", e.name, e.severity.value,
                e.description, str(e.safe_trigger).lower(), aux_str,
            ])


def _export_hk_sets(srdb: SRDB, path: Path) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow([
            "id", "name", "description",
            "parameters", "default_interval_ticks",
        ])
        for h in srdb.hk_sets:
            w.writerow([
                h.id, h.name, h.description,
                ";".join(h.parameters), h.default_interval_ticks,
            ])


# ------------------------------------------------------------------ #
# Import                                                              #
# ------------------------------------------------------------------ #

def import_csv(data_dir: Union[str, Path], spacecraft: Spacecraft) -> SRDB:
    """
    Import SRDB from CSV files in data_dir.
    Spacecraft must be provided separately (not in CSV).
    """
    d = Path(data_dir)
    return SRDB(
        spacecraft=spacecraft,
        parameters=_import_parameters(d / "parameters.csv"),
        telecommands=_import_telecommands(d / "telecommands.csv"),
        events=_import_events(d / "events.csv"),
        hk_sets=_import_hk_sets(d / "hk_sets.csv"),
    )


def _parse_optional_float(val: str) -> float | None:
    return float(val) if val.strip() else None


def _import_parameters(path: Path) -> list[Parameter]:
    params = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            conv = None
            if row["conv_type"]:
                conv = ParameterConversion(
                    type=ConversionType(row["conv_type"]),
                    slope=_parse_optional_float(row["conv_slope"]),
                    offset=_parse_optional_float(row["conv_offset"]),
                )
            lim = None
            if any(row[k] for k in [
                "limit_soft_low", "limit_soft_high",
                "limit_hard_low", "limit_hard_high"
            ]):
                lim = ParameterLimits(
                    soft_low=_parse_optional_float(row["limit_soft_low"]),
                    soft_high=_parse_optional_float(row["limit_soft_high"]),
                    hard_low=_parse_optional_float(row["limit_hard_low"]),
                    hard_high=_parse_optional_float(row["limit_hard_high"]),
                )
            params.append(Parameter(
                id=int(row["id_hex"], 16),
                name=row["name"],
                description=row["description"],
                type=ParameterType(row["type"]),
                unit=row["unit"] or None,
                conversion=conv,
                limits=lim,
            ))
    return params


def _import_telecommands(path: Path) -> list[Telecommand]:
    tcs = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            params = []
            if row["parameters"]:
                for item in row["parameters"].split(";"):
                    name, type_str = item.split(":")
                    params.append(TCParameter(
                        name=name,
                        type=ParameterType(type_str),
                    ))
            tcs.append(Telecommand(
                name=row["name"],
                description=row["description"],
                apid=int(row["apid_hex"], 16),
                service=int(row["service"]),
                subservice=int(row["subservice"]),
                parameters=params,
            ))
    return tcs


def _import_events(path: Path) -> list[Event]:
    from .model import EventAuxData
    events = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            aux = []
            if row["auxiliary_data"]:
                for item in row["auxiliary_data"].split(";"):
                    name, type_str = item.split(":")
                    aux.append(EventAuxData(
                        name=name,
                        type=ParameterType(type_str),
                    ))
            events.append(Event(
                id=int(row["id_hex"], 16),
                name=row["name"],
                severity=Severity(row["severity"]),
                description=row["description"],
                safe_trigger=row["safe_trigger"].lower() == "true",
                auxiliary_data=aux,
            ))
    return events


def _import_hk_sets(path: Path) -> list[HKSet]:
    hk_sets = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            hk_sets.append(HKSet(
                id=int(row["id"]),
                name=row["name"],
                description=row["description"],
                parameters=row["parameters"].split(";"),
                default_interval_ticks=int(row["default_interval_ticks"]),
            ))
    return hk_sets
