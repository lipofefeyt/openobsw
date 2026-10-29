"""SRDB loader: reference data loads, and invalid data is rejected on load."""

import shutil
from pathlib import Path

import pydantic
import pytest
import yaml

from obsw_srdb import SRDBLoader
from obsw_srdb.loader import SRDBValidationError

pytestmark = pytest.mark.requirement("OBSW-SRS-220")


@pytest.fixture
def data_copy(data_dir: Path, tmp_path: Path) -> Path:
    dst = tmp_path / "data"
    shutil.copytree(data_dir, dst)
    return dst


def edit(path: Path, fn) -> None:
    doc = yaml.safe_load(path.read_text(encoding="utf-8"))
    fn(doc)
    path.write_text(yaml.safe_dump(doc, sort_keys=False), encoding="utf-8")


def test_reference_data_loads(srdb):
    assert srdb.parameters and srdb.telecommands and srdb.events and srdb.hk_sets


def test_missing_file_is_rejected(data_copy):
    (data_copy / "events.yaml").unlink()
    with pytest.raises(FileNotFoundError):
        SRDBLoader.load(data_copy)


def test_unknown_hk_parameter_is_rejected(data_copy):
    edit(data_copy / "hk_sets.yaml", lambda d: d["hk_sets"][0]["parameters"].append("no_such_param"))
    with pytest.raises(SRDBValidationError, match="no_such_param"):
        SRDBLoader.load(data_copy)


def test_duplicate_parameter_id_is_rejected(data_copy):
    def dup(d):
        d["parameters"][1]["id"] = d["parameters"][0]["id"]
    edit(data_copy / "parameters.yaml", dup)
    with pytest.raises(SRDBValidationError, match="Duplicate parameter IDs"):
        SRDBLoader.load(data_copy)


def test_duplicate_telecommand_tuple_is_rejected(data_copy):
    def dup(d):
        clone = dict(d["telecommands"][0], name="clone_of_first")
        d["telecommands"].append(clone)
    edit(data_copy / "telecommands.yaml", dup)
    with pytest.raises(SRDBValidationError, match="Duplicate telecommand"):
        SRDBLoader.load(data_copy)


def test_apid_out_of_range_is_rejected(data_copy):
    edit(data_copy / "telecommands.yaml", lambda d: d["telecommands"][0].update(apid=0x7FF))
    with pytest.raises(pydantic.ValidationError, match="APID"):
        SRDBLoader.load(data_copy)


def test_type_and_ptc_pfc_must_agree(data_copy):
    edit(data_copy / "parameters.yaml", lambda d: d["parameters"][0].update(type="uint8", ptc=1, pfc=16))
    with pytest.raises(pydantic.ValidationError, match="expects"):
        SRDBLoader.load(data_copy)


# ---- Repeated TC arguments (count_from) ------------------------------------

@pytest.mark.requirement("OBSW-SRS-225")
def test_reference_tc31_has_counted_param_ids(srdb):
    tc = next(t for t in srdb.telecommands if (t.service, t.subservice) == (3, 1))
    assert [p.name for p in tc.parameters] == ["set_id", "interval_ticks", "n", "param_ids"]
    assert tc.parameters[-1].count_from == "n"


def _tc_with(params):
    from obsw_srdb.model import Telecommand
    return Telecommand(name="t", description="d", apid=0x010, service=200, subservice=1,
                       parameters=params)


@pytest.mark.requirement("OBSW-SRS-225")
@pytest.mark.parametrize("params, match", [
    ([{"name": "ids", "type": "uint16", "count_from": "n"}], "not an earlier argument"),
    ([{"name": "ids", "type": "uint16", "count_from": "n"},
      {"name": "n", "type": "uint8"}], "must be the last argument"),
    ([{"name": "n", "type": "int8"},
      {"name": "ids", "type": "uint16", "count_from": "n"}], "plain unsigned integer"),
    ([{"name": "a", "type": "uint8"},
      {"name": "b", "type": "uint8", "count_from": "a"},
      {"name": "c", "type": "uint8", "count_from": "b"}], "must be the last argument"),
])
def test_bad_count_from_is_rejected(params, match):
    with pytest.raises(pydantic.ValidationError, match=match):
        _tc_with(params)
