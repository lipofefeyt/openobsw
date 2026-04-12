"""Tests for SRDB CSV import/export round-trip."""
from __future__ import annotations

import tempfile
from pathlib import Path

import pytest

from obsw_srdb.loader import SRDBLoader
from obsw_srdb.csv_io import export_csv, import_csv

DATA_DIR = Path(__file__).parent.parent / "data"


class TestCsvIoSuite:

    def test_export_creates_all_files(self) -> None:
        """export_csv creates parameters, telecommands, events, hk_sets CSV."""
        srdb = SRDBLoader.load(DATA_DIR)
        with tempfile.TemporaryDirectory() as tmp:
            export_csv(srdb, tmp)
            files = {f for f in Path(tmp).iterdir()}
            names = {f.name for f in files}
            assert "parameters.csv" in names
            assert "telecommands.csv" in names
            assert "events.csv" in names
            assert "hk_sets.csv" in names

    def test_round_trip_parameters(self) -> None:
        """Parameters survive export → import unchanged."""
        srdb = SRDBLoader.load(DATA_DIR)
        with tempfile.TemporaryDirectory() as tmp:
            export_csv(srdb, tmp)
            srdb2 = import_csv(tmp, srdb.spacecraft)
            assert len(srdb.parameters) == len(srdb2.parameters)
            for p1, p2 in zip(srdb.parameters, srdb2.parameters):
                assert p1.id == p2.id
                assert p1.name == p2.name
                assert p1.type == p2.type
                assert p1.unit == p2.unit

    def test_round_trip_events(self) -> None:
        """Events survive export → import unchanged."""
        srdb = SRDBLoader.load(DATA_DIR)
        with tempfile.TemporaryDirectory() as tmp:
            export_csv(srdb, tmp)
            srdb2 = import_csv(tmp, srdb.spacecraft)
            assert len(srdb.events) == len(srdb2.events)
            for e1, e2 in zip(srdb.events, srdb2.events):
                assert e1.id == e2.id
                assert e1.name == e2.name
                assert e1.severity == e2.severity
                assert e1.safe_trigger == e2.safe_trigger

    def test_round_trip_telecommands(self) -> None:
        """Telecommands survive export → import unchanged."""
        srdb = SRDBLoader.load(DATA_DIR)
        with tempfile.TemporaryDirectory() as tmp:
            export_csv(srdb, tmp)
            srdb2 = import_csv(tmp, srdb.spacecraft)
            assert len(srdb.telecommands) == len(srdb2.telecommands)
            for tc1, tc2 in zip(srdb.telecommands, srdb2.telecommands):
                assert tc1.name == tc2.name
                assert tc1.service == tc2.service
                assert tc1.subservice == tc2.subservice

    def test_round_trip_hk_sets(self) -> None:
        """HK sets survive export → import unchanged."""
        srdb = SRDBLoader.load(DATA_DIR)
        with tempfile.TemporaryDirectory() as tmp:
            export_csv(srdb, tmp)
            srdb2 = import_csv(tmp, srdb.spacecraft)
            assert len(srdb.hk_sets) == len(srdb2.hk_sets)
            for h1, h2 in zip(srdb.hk_sets, srdb2.hk_sets):
                assert h1.id == h2.id
                assert h1.parameters == h2.parameters
