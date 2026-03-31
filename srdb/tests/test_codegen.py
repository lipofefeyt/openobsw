"""Tests for the SRDB C header code generator."""

from obsw_srdb.codegen import generate_header


class TestCodegen:
    def test_generates_string(self, srdb):
        header = generate_header(srdb)
        assert isinstance(header, str)
        assert len(header) > 0

    def test_header_guards(self, srdb):
        header = generate_header(srdb)
        assert "#ifndef OBSW_SRDB_GENERATED_H" in header
        assert "#define OBSW_SRDB_GENERATED_H" in header
        assert "#endif /* OBSW_SRDB_GENERATED_H */" in header

    def test_scid_present(self, srdb):
        header = generate_header(srdb)
        assert f"0x{srdb.spacecraft.scid:03X}U" in header

    def test_parameter_macros(self, srdb):
        header = generate_header(srdb)
        assert "SRDB_PARAM_OBC_TEMPERATURE" in header
        assert "SRDB_PARAM_OBC_VOLTAGE_3V3" in header
        assert "SRDB_PARAM_OBC_UPTIME" in header

    def test_event_macros(self, srdb):
        header = generate_header(srdb)
        assert "SRDB_EVENT_BOOT_COMPLETE" in header
        assert "SRDB_EVENT_WATCHDOG_EXPIRY" in header

    def test_hk_set_macros(self, srdb):
        header = generate_header(srdb)
        assert "SRDB_HK_NOMINAL_HK" in header
        assert "SRDB_HK_FDIR_HK" in header

    def test_tc_routing_macros(self, srdb):
        header = generate_header(srdb)
        assert "SRDB_TC_ARE_YOU_ALIVE_SVC" in header
        assert "SRDB_TC_ARE_YOU_ALIVE_SUBSVC" in header

    def test_safe_trigger_count(self, srdb):
        header = generate_header(srdb)
        count = len(srdb.safe_trigger_event_ids())
        assert f"SRDB_SAFE_TRIGGER_COUNT  {count}U" in header

    def test_safe_trigger_ids_array(self, srdb):
        header = generate_header(srdb)
        assert "SRDB_SAFE_TRIGGER_IDS" in header

    def test_all_parameters_have_macros(self, srdb):
        header = generate_header(srdb)
        for p in srdb.parameters:
            macro = f"SRDB_PARAM_{p.name.upper()}"
            assert macro in header, f"Missing macro {macro}"

    def test_all_events_have_macros(self, srdb):
        header = generate_header(srdb)
        for e in srdb.events:
            macro = f"SRDB_EVENT_{e.name.upper()}"
            assert macro in header, f"Missing macro {macro}"

    def test_cli_writes_file(self, srdb, tmp_path):
        from obsw_srdb.codegen import generate_header
        header = generate_header(srdb)
        out = tmp_path / "srdb_generated.h"
        out.write_text(header)
        content = out.read_text()
        assert "#ifndef OBSW_SRDB_GENERATED_H" in content