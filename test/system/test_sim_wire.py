"""obsw_sim over wire protocol v4: framing, mode gate, services and HK encoding."""

from __future__ import annotations

import binascii
import struct

import pytest

from conftest import (REPO, FAIL_ILLEGAL_SUBSVC, FAIL_ILLEGAL_SVC, FAIL_MEM_ACCESS,
                      FAIL_MODE_GATED, HK_AOCS, HK_DHS_OBC, MODE_NOMINAL, MODE_SAFE,
                      PARAM_BDOT_GAIN, PARAM_OBC_TEMPERATURE, PARAM_OBC_UPTIME, Stream,
                      parse)

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


@pytest.mark.requirement("OBSW-SRS-244")
def test_hk_due_in_a_tick_leaves_before_that_ticks_end_marker(run):
    s = Stream()
    s.ticks(10)  # dhs_obc_hk (set 3) is due every 10 ticks
    out = run(s)
    eots = [i for i, (k, _) in enumerate(out.events) if k == "eot"]
    assert len(eots) == 10
    last_tick = out.events[eots[8] + 1:eots[9]]
    assert any(k == "tm" and e.service == 3 and e.app[0] == HK_DHS_OBC for k, e in last_tick)


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
    s.ticks(10)
    out = run(s)
    modes = [struct.unpack(DHS_OBC_FMT, p)[0] for p in out.hk(HK_DHS_OBC)]
    assert modes == [2, 1, 0]


# ---- Mode-transition events ---------------------------------------------------

EV_SAFE_MODE_ENTRY, EV_STANDBY_ENTRY, EV_NOMINAL_ENTRY = 0x0002, 0x0004, 0x0005
MODE_EVENTS = (EV_SAFE_MODE_ENTRY, EV_STANDBY_ENTRY, EV_NOMINAL_ENTRY)


def by_tick(out):
    """TM of each end-of-tick-delimited segment, as (service, subservice, app)."""
    segments, cur = [], []
    for kind, item in out.events:
        if kind == "tm":
            cur.append((item.service, item.subservice, item.app))
        elif kind == "eot":
            segments.append(cur)
            cur = []
    return segments


def mode_events(tms):
    """(subservice, event_id, aux length) of the mode events in a TM list."""
    return [(sub, struct.unpack(">H", app[:2])[0], len(app) - 2) for svc, sub, app in tms
            if svc == 5 and struct.unpack(">H", app[:2])[0] in MODE_EVENTS]


@pytest.mark.requirement("OBSW-SRS-190")
def test_each_mode_transition_is_reported_once_with_the_tc_that_caused_it(run):
    s = Stream()
    s.ticks(3)                              # boot mode (SAFE) is not a transition
    s.nominal()                             # SAFE -> NOMINAL
    s.tc(8, 1, struct.pack(">HB", 2, 0))    # NOMINAL -> SAFE
    s.tc(8, 1, struct.pack(">HB", 2, 0))    # already SAFE: nothing to report
    s.tc(8, 1, struct.pack(">HB", 3, 0))    # SAFE -> STANDBY
    s.ticks(3)
    segments = by_tick(run(s))
    assert [mode_events(seg) for seg in segments] == [
        [], [], [],
        [(1, EV_NOMINAL_ENTRY, 0)],
        [(4, EV_SAFE_MODE_ENTRY, 0)],
        [],
        [(1, EV_STANDBY_ENTRY, 0)],
        [], [], [],
    ]
    # The event follows the S1 reports of the TC that changed the mode
    assert [(svc, sub) for svc, sub, _ in segments[3]] == [(1, 1), (1, 7), (5, 1)]


@pytest.mark.requirement("OBSW-SRS-190")
@pytest.mark.requirement("OBSW-SRS-295")
def test_safe_trigger_reports_safe_mode_entry_in_the_same_tick_without_retrigger(run):
    s = Stream()
    s.nominal()
    s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, EV_POWER_FAULT_3V3, 4))
    s.ticks(8, dt=1.0)
    segments = by_tick(run(s))
    high = [[struct.unpack(">H", app[:2])[0] for svc, sub, app in seg if (svc, sub) == (5, 4)]
            for seg in segments]
    # One tick carries the monitoring event, then the mode event; no other tick has either
    assert [ids for ids in high if ids] == [[EV_POWER_FAULT_3V3, EV_SAFE_MODE_ENTRY]]


@pytest.mark.requirement("OBSW-SRS-190")
@pytest.mark.requirement("OBSW-SRS-306")
def test_s19_reacts_to_a_mode_event(run):
    s = Stream()
    s.nominal()
    link = s.tc(19, 1, struct.pack(">H", EV_SAFE_MODE_ENTRY) + tc_packet(17, 1))
    s.tc(8, 1, struct.pack(">HB", 2, 0))    # SAFE: TC(17,1) is on the whitelist
    s.ticks(2)
    out = run(s)
    assert [t.subservice for t in out.s1_for(link)] == [1, 7]
    assert [t.subservice for t in out.s1_for(0x3FFF)] == [1, 7]
    assert any((t.service, t.subservice) == (17, 2) for t in out.tms)


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


@pytest.mark.requirement("OBSW-SRS-153")
def test_every_tm_packet_carries_onboard_time(run):
    """TM of every service is stamped with OBT seconds, also after TC(9,128)."""
    s = Stream()
    s.ticks(15)                                    # OBT 1.5 s
    s.nominal()                                    # S1, mode event
    set_obt = s.tc(9, 128, struct.pack(">IH", 1000, 0))
    s.tc(17, 1)                                    # S17
    s.tc(20, 3, struct.pack(">H", PARAM_BDOT_GAIN))  # S20
    s.tc(6, 9, struct.pack(">IHB", 0x100, 4, 0))   # S6
    s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.55, 0x0201, EV_POWER_FAULT_3V3, 4))
    s.ticks(30)                                    # HK, S12 event, SAFE mode event
    out = run(s)

    # One segment per input frame, in order: 15 ticks, 6 TCs, 30 ticks
    segments, cur = [], []
    for kind, item in out.events:
        if kind == "tm":
            cur.append(item)
        elif kind == "eot":
            segments.append(cur)
            cur = []
    assert len(segments) == 15 + 6 + 30

    for k, seg in enumerate(segments[:15], start=1):   # boot: OBT 0.1 .. 1.5 s
        assert all(t.time == k // 10 for t in seg), f"boot tick {k}"
    assert [t.time for t in segments[15]] == [1] * len(segments[15])   # nominal()
    # TC(9,128)'s own reports were built before the new time was taken over
    assert [(t.subservice, t.time) for t in out.s1_for(set_obt)] == [(1, 1), (7, 1)]
    for seg in segments[17:21]:                    # TCs after the set: 1000 s
        assert seg and all(t.time == 1000 for t in seg)
    for k, seg in enumerate(segments[21:], start=1):   # tick k after the set
        assert all(t.time == 1000 + k // 10 for t in seg), f"tick {k}"

    stamped = {t.service for seg in segments[17:] for t in seg}
    assert stamped == {1, 3, 5, 6, 17, 20}
    # The packet time equals obc_obt reported in the same tick's HK set 3
    for seg in segments[21:]:
        for t in seg:
            if (t.service, t.subservice) == (3, 25) and t.app[0] == HK_DHS_OBC:
                assert struct.unpack(DHS_OBC_FMT, t.app[1:])[1] == t.time


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
    events = [t for t in out.tms if t.service == 5
              and struct.unpack(">H", t.app[:2])[0] in (0x0201, 0x0202)]
    assert [(t.subservice, struct.unpack(">H", t.app[:2])[0]) for t in events] == [(2, 0x0202)]
    assert struct.unpack(">f", events[0].app[2:6])[0] == pytest.approx(3.0)


@pytest.mark.requirement("OBSW-SRS-172")
@pytest.mark.requirement("OBSW-SRS-292")
def test_s12_low_limit_on_negative_raw_temperature(run):
    """obc_temperature is int16 in the SRDB: raw -200 is below a -100 low limit."""
    s = Stream()
    s.nominal()
    s.tc(12, 3, s12_add(PARAM_OBC_TEMPERATURE, -100.0, 1250.0, 0x0201, 0x0202, 2))
    s.ticks(2)
    s.tc(20, 1, struct.pack(">Hi", PARAM_OBC_TEMPERATURE, -50))
    s.ticks(2)
    s.tc(20, 1, struct.pack(">Hi", PARAM_OBC_TEMPERATURE, -200))
    s.ticks(2)
    out = run(s)
    events = [t for t in out.tms if t.service == 5
              and struct.unpack(">H", t.app[:2])[0] in (0x0201, 0x0202)]
    assert [(t.subservice, struct.unpack(">H", t.app[:2])[0]) for t in events] == [(2, 0x0201)]
    assert struct.unpack(">f", events[0].app[2:6])[0] == pytest.approx(-200.0)


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


@pytest.mark.requirement("OBSW-SRS-292")
def test_s12_raised_limit_events_carry_the_aux_their_srdb_entry_declares(run):
    """Ground decodes TM(5,x) aux from the SRDB: it must be what S12 sends."""
    from obsw_srdb import SRDBLoader
    srdb = SRDBLoader.load(REPO / "srdb" / "data")
    events = {e.name: e for e in srdb.events}
    subservice = {"INFO": 1, "LOW": 2, "MEDIUM": 3, "HIGH": 4}
    for name in ("power_fault_3v3", "power_fault_5v",
                 "temperature_soft_limit", "temperature_hard_limit"):
        ev = events[name]
        assert [a.type.value for a in ev.auxiliary_data] == ["float32"], name
        sev = subservice[ev.severity.value]
        s = Stream()
        s.nominal()
        s.tc(12, 3, s12_add(PARAM_OBC_UPTIME, None, 2.5, 0x0201, ev.id, sev))
        s.ticks(6, dt=1.0)  # obc_uptime counts sim seconds: crosses 2.5 once
        raised = [t for t in run(s).tms if t.service == 5
                  and struct.unpack(">H", t.app[:2])[0] == ev.id]
        assert [t.subservice for t in raised] == [sev], name
        assert len(raised[0].app) == 2 + 4, f"{name}: aux is {len(raised[0].app) - 2} B"
        assert struct.unpack(">f", raised[0].app[2:])[0] == pytest.approx(3.0), name


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


# ---- S11 ---------------------------------------------------------------------

@pytest.mark.requirement("OBSW-SRS-282")
@pytest.mark.requirement("OBSW-SRS-285")
def test_s11_releases_scheduled_tc_at_its_obt(run):
    scheduled = tc_packet(20, 1, struct.pack(">Hf", PARAM_BDOT_GAIN, 20000.0), seq=0x2AAA)
    s = Stream()
    s.tc(9, 128, struct.pack(">IH", 1000, 0))  # OBT = 1000 s
    s.nominal()
    ins = s.tc(11, 4, struct.pack(">IH", 1008, 0) + scheduled)
    s.ticks(16, dt=1.0)
    out = run(s)
    assert [t.subservice for t in out.s1_for(ins)] == [1, 7]
    assert [t.subservice for t in out.s1_for(0x2AAA)] == [1, 7]
    gains = [struct.unpack(">fff", p)[0] for p in out.hk(HK_AOCS)]
    assert gains[0] == pytest.approx(1.0e4)   # before 1008 s
    assert gains[-1] == pytest.approx(2.0e4)  # after release


@pytest.mark.requirement("OBSW-SRS-282")
@pytest.mark.requirement("OBSW-SRS-182")
def test_s11_release_is_mode_gated_at_release_time(run):
    scheduled = tc_packet(20, 1, struct.pack(">Hf", PARAM_BDOT_GAIN, 20000.0), seq=0x2AAA)
    s = Stream()
    s.tc(9, 128, struct.pack(">IH", 1000, 0))
    s.nominal()
    s.tc(11, 4, struct.pack(">IH", 1003, 0) + scheduled)  # accepted in NOMINAL
    s.tc(8, 1, struct.pack(">HB", 2, 0))                   # SAFE before release
    s.ticks(6, dt=1.0)
    out = run(s)
    [rej] = out.s1_for(0x2AAA)
    assert (rej.subservice, rej.failure_code) == (2, FAIL_MODE_GATED)


@pytest.mark.requirement("OBSW-SRS-096")
def test_tc33_deletes_defined_set_and_refuses_boot_set(run):
    s = Stream()
    s.nominal()
    define = struct.pack(">BIBHH", 20, 1, 2, PARAM_OBC_UPTIME, PARAM_BDOT_GAIN)
    s.tc(3, 1, define)
    s.tc(3, 5, struct.pack(">BI", 20, 1))
    s.ticks(2, dt=1.0)
    s.tc(3, 6, struct.pack(">B", 20))
    delete = s.tc(3, 3, bytes([1, 20]))
    redefine = s.tc(3, 1, define)                  # same SID free again
    boot = s.tc(3, 3, bytes([1, HK_DHS_OBC]))      # dhs_obc_hk is a boot-time set
    out = run(s)
    assert out.hk(20), "set 20 never reported before deletion"
    assert [t.subservice for t in out.s1_for(delete)] == [1, 7]
    assert [t.subservice for t in out.s1_for(redefine)] == [1, 7]
    assert [(t.subservice, t.failure_code) for t in out.s1_for(boot)] == [(2, 0x0005)]


# ---- HK layout against the SRDB -----------------------------------------------

@pytest.mark.requirement("OBSW-SRS-097")
def test_every_boot_hk_set_matches_its_srdb_layout(run):
    from obsw_srdb import SRDBLoader
    srdb = SRDBLoader.load(REPO / "srdb" / "data")
    params = {p.name: p for p in srdb.parameters}
    width = {"uint8": 1, "int8": 1, "uint16": 2, "int16": 2,
             "uint32": 4, "int32": 4, "float32": 4}
    expected = {h.id: sum(width[params[n].type.value] for n in h.parameters)
                for h in srdb.hk_sets}

    s = Stream()
    s.ticks(60)  # every boot-time set has reported at least once by tick 60
    out = run(s)
    seen = {t.app[0]: len(t.app) - 1 for t in out.tms if (t.service, t.subservice) == (3, 25)}
    assert {1, 2, 3, 4, 6} <= set(seen), f"sets reported: {sorted(seen)}"
    for sid, length in seen.items():
        assert length == expected[sid], f"set {sid}: {length} B on board, {expected[sid]} B in SRDB"


# ---- Thermal input (wire protocol v4) -------------------------------------------

EV_TEMPERATURE_HARD_LIMIT = 0x0051  # HIGH, safe_trigger: true in srdb/data/events.yaml
NOMINAL_HK_FMT = ">hHHI"            # obc_temperature, v3v3, v5v, uptime (set 1)
SENSOR_BODY_V3 = 47


def hk_temperatures(out):
    return [struct.unpack(NOMINAL_HK_FMT, p)[0] for p in out.hk(1)]


@pytest.mark.requirement("OBSW-SRS-245")
def test_sensor_frame_of_another_length_stops_the_sim(run_process):
    s = Stream()
    s.ticks(2)
    s.sensor(bytes(SENSOR_BODY_V3))   # a v3 sender
    s.ticks(2)
    proc = run_process(s)
    assert b"(wire protocol v4)" in proc.stderr
    assert proc.returncode == 2
    assert b"sensor frame of 47 B, wire protocol v4 expects 50 B (v3 sender?)" in proc.stderr
    # The two good ticks were answered; nothing after the rejected frame
    events = parse(proc.stdout).events
    assert [k for k, _ in events if k != "tm"] == ["act", "eot", "act", "eot"]


@pytest.mark.requirement("OBSW-SRS-246")
def test_valid_thermal_input_drives_obc_temperature(run):
    s = Stream()
    s.nominal()
    s.ticks(10, temperature_raw=900)                     # 50 degC
    s.ticks(10)                                          # invalid: value held
    s.tc(20, 1, struct.pack(">HI", PARAM_OBC_TEMPERATURE, 700))
    s.ticks(10)                                          # TC(20,1) still sets it
    s.ticks(10, temperature_raw=-150)                    # frame overrides, negative raw
    assert hk_temperatures(run(s)) == [900, 900, 700, -150]


@pytest.mark.requirement("OBSW-SRS-246")
@pytest.mark.requirement("OBSW-SRS-295")
def test_thermal_fault_commands_safe_and_cleared_value_is_observable_in_safe(run):
    """Recovery-evidence outcome A: the anomaly clears while the OBSW stays in SAFE."""
    s = Stream()
    s.nominal()
    s.tc(12, 3, s12_add(PARAM_OBC_TEMPERATURE, None, 1250.0, 0x0050,
                        EV_TEMPERATURE_HARD_LIMIT, 4))   # 85 degC
    s.ticks(10, temperature_raw=1000)
    s.ticks(10, temperature_raw=1300)                    # 90 degC: fault
    s.ticks(20, temperature_raw=1000)                    # cleared, no TC sent
    out = run(s)

    high = [(struct.unpack(">H", t.app[:2])[0], t.app[2:])
            for t in out.tms if (t.service, t.subservice) == (5, 4)]
    assert [ev for ev, _ in high] == [EV_TEMPERATURE_HARD_LIMIT, EV_SAFE_MODE_ENTRY]
    assert struct.unpack(">f", high[0][1])[0] == pytest.approx(1300.0)
    assert hk_temperatures(out) == [1000, 1300, 1000, 1000]
    modes = [struct.unpack(DHS_OBC_FMT, p)[0] for p in out.hk(HK_DHS_OBC)]
    assert modes[-2:] == [MODE_SAFE, MODE_SAFE]          # no automatic recovery
    transitions = [ev for _, ev, _ in mode_events([tm for seg in by_tick(out) for tm in seg])]
    assert transitions == [EV_NOMINAL_ENTRY, EV_SAFE_MODE_ENTRY]
