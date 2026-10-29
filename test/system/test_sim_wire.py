"""obsw_sim over wire protocol v3: framing, mode gate, services and HK encoding."""

from __future__ import annotations

import binascii
import struct

import pytest

from conftest import (FAIL_ILLEGAL_SUBSVC, FAIL_ILLEGAL_SVC, FAIL_MEM_ACCESS,
                      FAIL_MODE_GATED, HK_AOCS, HK_DHS_OBC, MODE_NOMINAL, MODE_SAFE,
                      PARAM_BDOT_GAIN, PARAM_OBC_UPTIME, Stream)

DHS_OBC_FMT = ">BIBBBHB"  # mode, obt, wd, mem, health, reset, cpu (opensvf decoder)


# ---- Framing ---------------------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-240")
def test_each_sensor_frame_gives_actuator_frame_and_end_of_tick(run):
    s = Stream()
    s.ticks(3)
    out = run(s)
    order = [k for k, _ in out.events if k in ("act", "eot")]
    assert order == ["act", "eot"] * 3
    assert all(len(e) == 29 for k, e in out.events if k == "act")


@pytest.mark.requirement("OBSW-SRS-241")
@pytest.mark.requirement("OBSW-SRS-160")
def test_ping_reports_arrive_in_order_before_end_of_tick(run):
    s = Stream()
    s.tc(17, 1)
    out = run(s)
    first_eot = next(i for i, (k, _) in enumerate(out.events) if k == "eot")
    tms = [(e.service, e.subservice) for k, e in out.events[:first_eot] if k == "tm"]
    assert tms == [(1, 1), (17, 2), (1, 7)]


# ---- Mode machine and gate ------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-242")
@pytest.mark.requirement("OBSW-SRS-186")
def test_boots_in_safe_and_reports_srdb_mode_values(run):
    s = Stream()
    s.ticks(12)
    s.nominal()
    s.ticks(12)
    out = run(s)
    modes = [struct.unpack(DHS_OBC_FMT, p)[0] for p in out.hk(HK_DHS_OBC)]
    assert modes[0] == MODE_SAFE
    assert modes[-1] == MODE_NOMINAL


@pytest.mark.requirement("OBSW-SRS-182")
@pytest.mark.requirement("OBSW-SRS-063")
@pytest.mark.requirement("OBSW-SRS-142")
def test_safe_rejects_non_whitelisted_tc_until_nominal(run):
    s = Stream()
    set_param = struct.pack(">HI", PARAM_BDOT_GAIN, 0x461C4000)  # 10000.0f
    gated = s.tc(20, 1, set_param)
    get_ok = s.tc(20, 3, struct.pack(">H", PARAM_BDOT_GAIN))
    to_nominal = s.nominal()
    accepted = s.tc(20, 1, set_param)
    out = run(s)

    [rej] = out.s1_for(gated)
    assert (rej.subservice, rej.failure_code) == (2, FAIL_MODE_GATED)
    assert [t.subservice for t in out.s1_for(get_ok)] == [1, 7]
    assert [t.subservice for t in out.s1_for(to_nominal)] == [1, 7]
    assert [t.subservice for t in out.s1_for(accepted)] == [1, 7]


@pytest.mark.requirement("OBSW-SRS-142")
def test_s8_safe_and_standby_functions_change_reported_mode(run):
    s = Stream()
    s.nominal()
    s.ticks(10)
    s.tc(8, 1, struct.pack(">HB", 2, 0))  # SAFE
    s.ticks(10)
    s.tc(8, 1, struct.pack(">HB", 3, 0))  # STANDBY
    s.ticks(11)  # HK from a tick leaves with the next frame
    out = run(s)
    modes = [struct.unpack(DHS_OBC_FMT, p)[0] for p in out.hk(HK_DHS_OBC)]
    assert modes == [2, 1, 0]


@pytest.mark.requirement("OBSW-SRS-061")
def test_unrouted_tcs_get_specific_tm12_codes(run):
    s = Stream()
    s.nominal()
    bad_svc = s.tc(99, 1)
    bad_sub = s.tc(17, 9)
    out = run(s)
    assert [(t.subservice, t.failure_code) for t in out.s1_for(bad_svc)] == [(2, FAIL_ILLEGAL_SVC)]
    assert [(t.subservice, t.failure_code) for t in out.s1_for(bad_sub)] == [(2, FAIL_ILLEGAL_SUBSVC)]


# ---- S6 --------------------------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-243")
@pytest.mark.requirement("OBSW-SRS-123")
def test_s6_patch_ram_load_dump_check_and_bounds(run):
    pattern = bytes([0xDE, 0xAD, 0xBE, 0xEF])
    s = Stream()
    s.nominal()
    load = s.tc(6, 2, struct.pack(">IH", 0x100, 4) + pattern)
    dump = s.tc(6, 9, struct.pack(">IHB", 0x100, 4, 0))
    check = s.tc(6, 5, struct.pack(">IHBH", 0x100, 4, 0, binascii.crc_hqx(pattern, 0xFFFF)))
    straddle = s.tc(6, 9, struct.pack(">IHB", 4094, 4, 0))
    out = run(s)

    assert [t.subservice for t in out.s1_for(load)] == [1, 7]
    assert [t.subservice for t in out.s1_for(dump)] == [1, 7]
    assert [t.subservice for t in out.s1_for(check)] == [1, 7]
    [dumped] = [t for t in out.tms if (t.service, t.subservice) == (6, 6)]
    assert dumped.app[5:] == pattern
    assert any((t.service, t.subservice) == (6, 10) for t in out.tms)
    assert [(t.subservice, t.failure_code) for t in out.s1_for(straddle)] == [(2, FAIL_MEM_ACCESS)]


# ---- S9 and HK encoding ----------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-152")
@pytest.mark.requirement("OBSW-SRS-092")
def test_set_obt_is_reported_big_endian_in_dhs_obc_hk(run):
    s = Stream()
    s.tc(9, 128, struct.pack(">IH", 1000, 0x8000))  # 1000.5 s, allowed in SAFE
    s.ticks(12)
    out = run(s)
    obt = struct.unpack(DHS_OBC_FMT, out.hk(HK_DHS_OBC)[0])[1]
    assert obt == 1001  # 1000.5 s + 10 x 0.1 s, truncated to seconds


@pytest.mark.requirement("OBSW-SRS-204")
@pytest.mark.requirement("OBSW-SRS-092")
def test_gain_set_by_s20_appears_in_aocs_hk_as_big_endian_float(run):
    s = Stream()
    s.ticks(6)
    s.nominal()
    s.tc(20, 1, struct.pack(">Hf", PARAM_BDOT_GAIN, 20000.0))
    s.ticks(12)
    out = run(s)
    gains = [struct.unpack(">fff", p)[0] for p in out.hk(HK_AOCS)]
    assert gains[0] == pytest.approx(1.0e4)
    assert gains[-1] == pytest.approx(2.0e4)


# ---- S3 TC(3,1) --------------------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-093")
@pytest.mark.requirement("OBSW-SRS-243")
def test_defined_hk_set_reports_live_parameters_once_enabled(run):
    s = Stream()
    s.nominal()
    define = s.tc(3, 1, struct.pack(">BIBHH", 20, 1, 2, PARAM_OBC_UPTIME, PARAM_BDOT_GAIN))
    s.ticks(3, dt=1.0)
    s.tc(3, 5, struct.pack(">BI", 20, 1))
    s.ticks(4, dt=1.0)
    out = run(s)
    assert [t.subservice for t in out.s1_for(define)] == [1, 7]
    reports = [struct.unpack(">If", p) for p in out.hk(20)]
    assert reports, "set 20 never reported after TC(3,5)"
    uptimes = [u for u, _ in reports]
    assert uptimes == sorted(uptimes) and uptimes[-1] > uptimes[0]
    assert all(g == pytest.approx(1.0e4) for _, g in reports)


# ---- S12 ---------------------------------------------------------------------

EV_POWER_FAULT_3V3 = 0x0020  # safe_trigger: true in srdb/data/events.yaml


def s12_add(param_id, low, high, ev_low, ev_high, severity):
    nan = float("nan")
    return struct.pack(">HffHHB", param_id, nan if low is None else low,
                       nan if high is None else high, ev_low, ev_high, severity)


@pytest.mark.requirement("OBSW-SRS-292")
@pytest.mark.requirement("OBSW-SRS-296")
def test_s12_limit_crossing_raises_event_once(run):
    s = Stream()
    s.nominal()
    add = s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, 0x0202, 2))
    s.ticks(6, dt=1.0)  # obc_uptime counts sim seconds: crosses 2.5 once
    out = run(s)
    assert [t.subservice for t in out.s1_for(add)] == [1, 7]
    events = [t for t in out.tms if t.service == 5]
    assert [(t.subservice, struct.unpack(">H", t.app[:2])[0]) for t in events] == [(2, 0x0202)]
    assert struct.unpack(">f", events[0].app[2:6])[0] == pytest.approx(3.0)


@pytest.mark.requirement("OBSW-SRS-295")
def test_s12_high_event_on_safe_trigger_list_commands_safe(run):
    s = Stream()
    s.nominal()
    s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, EV_POWER_FAULT_3V3, 4))
    s.ticks(12, dt=1.0)
    out = run(s)
    assert any(t.service == 5 and t.subservice == 4 for t in out.tms)
    modes = [struct.unpack(DHS_OBC_FMT, p)[0] for p in out.hk(HK_DHS_OBC)]
    assert modes[-1] == MODE_SAFE


@pytest.mark.requirement("OBSW-SRS-182")
def test_s12_definitions_need_nominal(run):
    s = Stream()
    add = s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, 0x0202, 2))
    out = run(s)
    assert [(t.subservice, t.failure_code) for t in out.s1_for(add)] == [(2, FAIL_MODE_GATED)]


# ---- S19 ---------------------------------------------------------------------

def tc_packet(svc, sub, app=b"", seq=0x3FFF, apid=0x010):
    """A bare PUS-C TC space packet, as stored in a TC(19,1) action."""
    body = bytes([0x11, svc, sub, 0x00, 0x00]) + app
    return struct.pack(">HHH", 0x1800 | apid, 0xC000 | seq, len(body) - 1) + body


@pytest.mark.requirement("OBSW-SRS-302")
@pytest.mark.requirement("OBSW-SRS-306")
def test_s12_event_triggers_s19_action_that_retunes_gain(run):
    action = tc_packet(20, 1, struct.pack(">Hf", PARAM_BDOT_GAIN, 20000.0))
    s = Stream()
    s.nominal()
    link = s.tc(19, 1, struct.pack(">H", 0x0202) + action)
    # Crossing at t=8 s: the AOCS HK report at tick 5 still shows the old gain
    s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 7.5, 0x0201, 0x0202, 2))
    s.ticks(16, dt=1.0)
    out = run(s)
    assert [t.subservice for t in out.s1_for(link)] == [1, 7]
    # The stored action is verified like an uplinked TC (seq 0x3FFF)
    assert [t.subservice for t in out.s1_for(0x3FFF)] == [1, 7]
    gains = [struct.unpack(">fff", p)[0] for p in out.hk(HK_AOCS)]
    assert gains[0] == pytest.approx(1.0e4)
    assert gains[-1] == pytest.approx(2.0e4)


@pytest.mark.requirement("OBSW-SRS-302")
@pytest.mark.requirement("OBSW-SRS-182")
def test_s19_action_is_still_mode_gated_in_safe(run):
    action = tc_packet(20, 1, struct.pack(">Hf", PARAM_BDOT_GAIN, 20000.0))
    s = Stream()
    s.nominal()
    s.tc(19, 1, struct.pack(">H", 0x0202) + action)
    s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, 0x0202, 2))
    s.tc(8, 1, struct.pack(">HB", 2, 0))  # back to SAFE before the event
    s.ticks(6, dt=1.0)
    out = run(s)
    [rej] = out.s1_for(0x3FFF)
    assert (rej.subservice, rej.failure_code) == (2, FAIL_MODE_GATED)
