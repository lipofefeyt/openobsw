"""Tests for the XTCE mission database generator."""

import xml.etree.ElementTree as ET

import pytest

from obsw_srdb.codegen import generate_xtce, _param_bits

_NS = "http://www.omg.org/space/xtce"


def _tag(name: str) -> str:
    return f"{{{_NS}}}{name}"


def _find_all(root: ET.Element, *path: str) -> list[ET.Element]:
    """Walk a sequence of tag names from root."""
    nodes = [root]
    for tag in path:
        nodes = [child for n in nodes for child in n if child.tag == _tag(tag)]
    return nodes


class TestXTCEStructure:
    def test_generates_string(self, srdb):
        xtce = generate_xtce(srdb)
        assert isinstance(xtce, str)
        assert len(xtce) > 0

    def test_xml_declaration(self, srdb):
        xtce = generate_xtce(srdb)
        assert xtce.startswith('<?xml version="1.0" encoding="UTF-8"?>')

    def test_well_formed_xml(self, srdb):
        xtce = generate_xtce(srdb)
        root = ET.fromstring(xtce)
        assert root is not None

    def test_root_is_space_system(self, srdb):
        xtce = generate_xtce(srdb)
        root = ET.fromstring(xtce)
        assert root.tag == _tag("SpaceSystem")

    def test_spacecraft_name_in_root(self, srdb):
        xtce = generate_xtce(srdb)
        root = ET.fromstring(xtce)
        assert srdb.spacecraft.name.replace(" ", "_") == root.get("name")

    def test_has_telemetry_metadata(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        tm = root.find(_tag("TelemetryMetaData"))
        assert tm is not None

    def test_has_command_metadata(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        cmd = root.find(_tag("CommandMetaData"))
        assert cmd is not None


class TestParameterTypes:
    def test_all_mission_params_have_types(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        pts = root.find(f".//{_tag('ParameterTypeSet')}")
        type_names = {el.get("name") for el in pts}

        for p in srdb.parameters:
            if p.enumeration:
                assert f"{p.name.upper()}_t" in type_names
            elif p.conversion or p.limits:
                assert f"{p.name.upper()}_t" in type_names

    def test_enum_params_have_enumeration_type(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        enum_type_names = {
            el.get("name")
            for el in root.iter(_tag("EnumeratedParameterType"))
        }
        for p in srdb.parameters:
            if p.enumeration:
                assert f"{p.name.upper()}_t" in enum_type_names

    def test_enum_entries_present(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        for p in srdb.parameters:
            if not p.enumeration:
                continue
            tname = f"{p.name.upper()}_t"
            found = None
            for el in root.iter(_tag("EnumeratedParameterType")):
                if el.get("name") == tname:
                    found = el
                    break
            assert found is not None, f"No EnumeratedParameterType for {tname}"
            labels = {e.get("label") for e in found.iter(_tag("Enumeration"))}
            for entry in p.enumeration:
                assert entry.label in labels

    def test_calibrated_params_have_polynomial(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        for p in srdb.parameters:
            if not p.conversion:
                continue
            tname = f"{p.name.upper()}_t"
            found = None
            for el in root.iter(_tag("IntegerParameterType")):
                if el.get("name") == tname:
                    found = el
                    break
            if found is None:
                for el in root.iter(_tag("FloatParameterType")):
                    if el.get("name") == tname:
                        found = el
                        break
            assert found is not None
            assert found.find(f".//{_tag('PolynomialCalibrator')}") is not None

    def test_limited_params_have_alarm(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        for p in srdb.parameters:
            if not p.limits:
                continue
            tname = f"{p.name.upper()}_t"
            for el in root.iter():
                if el.get("name") == tname:
                    assert el.find(f".//{_tag('StaticAlarmRanges')}") is not None
                    break

    def test_system_types_present(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        type_names = {el.get("name") for el in root.iter(_tag("IntegerParameterType"))}
        for required in ("uint8_t", "uint16_t", "uint32_t", "uint11_t"):
            assert required in type_names


class TestParameterSet:
    def test_all_mission_params_present(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        param_names = {
            el.get("name")
            for el in root.iter(_tag("Parameter"))
        }
        for p in srdb.parameters:
            assert p.name.upper() in param_names

    def test_header_params_present(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        param_names = {el.get("name") for el in root.iter(_tag("Parameter"))}
        for required in ("APID", "SERVICE_TYPE", "SERVICE_SUBTYPE",
                         "HK_SID", "TIMESTAMP"):
            assert required in param_names


class TestContainerSet:
    def test_ccsds_base_container_exists(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        assert "CCSDSPacket" in names

    def test_pus_packet_container_exists(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        assert "PUSPacket" in names

    def test_tm_3_25_base_exists(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        assert "TM_3_25" in names

    def test_hk_containers_for_all_sets(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        for hk in srdb.hk_sets:
            assert f"TM_3_25_{hk.name.upper()}" in names

    def test_hk_container_param_count(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        for hk in srdb.hk_sets:
            cname = f"TM_3_25_{hk.name.upper()}"
            container = None
            for el in root.iter(_tag("SequenceContainer")):
                if el.get("name") == cname:
                    container = el
                    break
            assert container is not None
            entry_list = container.find(_tag("EntryList"))
            assert entry_list is not None
            entries = list(entry_list.iter(_tag("ParameterRefEntry")))
            assert len(entries) == len(hk.parameters)

    def test_hk_bit_offsets_non_overlapping(self, srdb):
        """Parameters in each HK container must have strictly increasing bit offsets."""
        root = ET.fromstring(generate_xtce(srdb))
        for hk in srdb.hk_sets:
            cname = f"TM_3_25_{hk.name.upper()}"
            container = next(
                el for el in root.iter(_tag("SequenceContainer"))
                if el.get("name") == cname
            )
            entry_list = container.find(_tag("EntryList"))
            offsets = [
                int(e.find(f".//{_tag('FixedValue')}").text)
                for e in entry_list.iter(_tag("ParameterRefEntry"))
            ]
            assert offsets == sorted(offsets), f"{cname}: offsets not sorted"
            assert len(offsets) == len(set(offsets)), f"{cname}: duplicate offsets"

    def test_hk_bit_offsets_correct(self, srdb):
        """First parameter of each HK container starts at bit 144 (after HK_SID)."""
        root = ET.fromstring(generate_xtce(srdb))
        for hk in srdb.hk_sets:
            if not hk.parameters:
                continue
            cname = f"TM_3_25_{hk.name.upper()}"
            container = next(
                el for el in root.iter(_tag("SequenceContainer"))
                if el.get("name") == cname
            )
            entry_list = container.find(_tag("EntryList"))
            first_offset = int(
                next(iter(entry_list.iter(_tag("ParameterRefEntry"))))
                .find(f".//{_tag('FixedValue')}").text
            )
            assert first_offset == 144, (
                f"{cname}: first param should start at bit 144, got {first_offset}"
            )

    def test_s17_ping_container_exists(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        assert "TM_17_2" in names

    def test_s5_event_containers_exist(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        names = {el.get("name") for el in root.iter(_tag("SequenceContainer"))}
        for i in range(1, 5):
            assert f"TM_5_{i}" in names


class TestCommandMetaData:
    def test_metacommand_for_each_tc(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        mc_names = {el.get("name") for el in root.iter(_tag("MetaCommand"))}
        for tc in srdb.telecommands:
            expected = f"TC_{tc.service}_{tc.subservice}_{tc.name.upper()}"
            assert expected in mc_names

    def test_tc_arguments_present(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        for tc in srdb.telecommands:
            if not tc.parameters:
                continue
            mc_name = f"TC_{tc.service}_{tc.subservice}_{tc.name.upper()}"
            mc = next(
                el for el in root.iter(_tag("MetaCommand"))
                if el.get("name") == mc_name
            )
            arg_names = {a.get("name") for a in mc.iter(_tag("Argument"))}
            for arg in tc.parameters:
                assert arg.name.upper() in arg_names


class TestDHSOBCHKSet:
    """Targeted tests for the new DHS OBC HK set that opensvf consumes."""

    def test_dhs_obc_hk_set_exists(self, srdb):
        assert srdb.hk_set_by_id(3) is not None
        assert srdb.hk_set_by_id(3).name == "dhs_obc_hk"

    def test_dhs_obc_hk_has_seven_params(self, srdb):
        hk = srdb.hk_set_by_id(3)
        assert len(hk.parameters) == 7

    def test_dhs_obc_hk_spid(self, srdb):
        assert srdb.hk_set_by_id(3).spid == 1003

    def test_obc_mode_is_enumerated(self, srdb):
        p = srdb.parameter_by_name("obc_mode")
        assert p is not None
        assert p.enumeration is not None
        labels = {e.label for e in p.enumeration}
        assert {"SAFE", "NOMINAL", "PAYLOAD"} == labels

    def test_obc_health_is_enumerated(self, srdb):
        p = srdb.parameter_by_name("obc_health")
        assert p is not None
        assert p.enumeration is not None
        labels = {e.label for e in p.enumeration}
        assert {"NOMINAL", "DEGRADED", "FAILED"} == labels

    def test_dhs_obc_params_in_xtce_container(self, srdb):
        root = ET.fromstring(generate_xtce(srdb))
        container = next(
            el for el in root.iter(_tag("SequenceContainer"))
            if el.get("name") == "TM_3_25_DHS_OBC_HK"
        )
        entry_list = container.find(_tag("EntryList"))
        param_refs = [
            e.get("parameterRef")
            for e in entry_list.iter(_tag("ParameterRefEntry"))
        ]
        expected = [
            "OBC_MODE", "OBC_OBT", "OBC_WATCHDOG_STATUS",
            "OBC_MEMORY_USED_PCT", "OBC_HEALTH", "OBC_RESET_COUNT",
            "OBC_CPU_LOAD",
        ]
        assert param_refs == expected
