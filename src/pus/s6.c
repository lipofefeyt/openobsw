/**
 * @file s6.c
 * @brief PUS-C Service 6 — Memory Management implementation.
 *
 * TC(6,2) loads data into a writable region; TC(6,5) verifies a readable
 * region with CRC-16/CCITT; TC(6,9) dumps a readable region as TM(6,6).
 * Every ground address is resolved through the context's region table
 * (s6_resolve) before any memory is touched.
 */
#include "obsw/pus/s6.h"

#include "obsw/ccsds/tc_frame.h" /* obsw_crc16_ccitt */
#include "obsw/util/bytes.h"

#include <string.h>

/* Maximum data bytes in a single TM(6,6) dump packet.
 * Layout: PUS sec hdr(11) + start_addr(4) + mem_id(1) + data
 */
#define S6_DUMP_HDR_OVERHEAD (OBSW_PUS_TM_SEC_HDR_LEN + 5U)
#define S6_MAX_DUMP_DATA                                                                           \
    ((uint16_t)(OBSW_TM_MAX_PACKET_LEN - OBSW_SP_PRIMARY_HEADER_LEN - S6_DUMP_HDR_OVERHEAD))

/* ------------------------------------------------------------------ */
/* Region lookup                                                       */
/* ------------------------------------------------------------------ */

/* Resolve ground range [addr, addr + len) to a local pointer if it lies
 * entirely inside one region granting @p access; NULL otherwise. The
 * bounds test is written so that no addition can overflow. */
static const uint8_t *
s6_resolve(const obsw_s6_ctx_t *s, uint32_t addr, uint16_t len, uint8_t access)
{
    if (!s->regions)
        return NULL;

    for (uint8_t i = 0; i < s->region_count; i++) {
        const obsw_s6_region_t *r = &s->regions[i];
        if ((r->access & access) != access || addr < r->start)
            continue;
        uint32_t off = addr - r->start;
        if (off > r->size || len > r->size - off)
            continue;
        return r->base + off;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* TC(6,2) — load memory area                                          */
/* ------------------------------------------------------------------ */

int obsw_s6_load(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)respond;
    obsw_s6_ctx_t *s = (obsw_s6_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < OBSW_S6_LOAD_MIN_LEN) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint32_t addr     = obsw_be32(tc->user_data);
    uint16_t data_len = obsw_be16(tc->user_data + 4);

    /* 32-bit sum: MIN_LEN + data_len must not wrap past a short TC */
    if ((uint32_t)OBSW_S6_LOAD_MIN_LEN + data_len > tc->user_data_len) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    const uint8_t *dst = s6_resolve(s, addr, data_len, OBSW_S6_ACCESS_WRITE);
    if (!dst) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_MEM_ACCESS);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);

    /* Region is WRITE, so its memory is writable (see obsw_s6_region_t) */
    memcpy((uint8_t *)(uintptr_t)dst, tc->user_data + OBSW_S6_LOAD_MIN_LEN, data_len);

    obsw_s1_completion_success(s->s1, tc);
    return 0;
}

/* ------------------------------------------------------------------ */
/* TC(6,5) — check memory area                                         */
/* ------------------------------------------------------------------ */

int obsw_s6_check(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)respond;
    obsw_s6_ctx_t *s = (obsw_s6_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < OBSW_S6_CHECK_LEN) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint32_t addr   = obsw_be32(tc->user_data);
    uint16_t length = obsw_be16(tc->user_data + 4);
    /* tc->user_data[6] = mem_id (ignored for now) */
    uint16_t expected = obsw_be16(tc->user_data + 7);

    const uint8_t *src = s6_resolve(s, addr, length, OBSW_S6_ACCESS_READ);
    if (!src) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_MEM_ACCESS);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);

    uint16_t actual = obsw_crc16_ccitt(src, length);

    if (actual == expected) {
        /* TM(6,10) — check success: start_addr(4) + length(2) */
        uint8_t data[6];
        memcpy(data, tc->user_data, 4);         /* start_addr */
        memcpy(data + 4, tc->user_data + 4, 2); /* length     */
        uint16_t seq = s->msg_counter++;
        obsw_pus_tm_build(
            s->tm_store, s->apid, seq, 6, 10, seq, 0, s->timestamp, data, sizeof(data));
        obsw_s1_completion_success(s->s1, tc);
    } else {
        /* TM(6,11) — check failure: start_addr(4) + length(2) + actual_crc(2) */
        uint8_t data[8];
        memcpy(data, tc->user_data, 4);
        memcpy(data + 4, tc->user_data + 4, 2);
        data[6]      = (uint8_t)(actual >> 8);
        data[7]      = (uint8_t)(actual & 0xFFU);
        uint16_t seq = s->msg_counter++;
        obsw_pus_tm_build(
            s->tm_store, s->apid, seq, 6, 11, seq, 0, s->timestamp, data, sizeof(data));
        obsw_s1_completion_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* TC(6,9) — dump memory area                                          */
/* ------------------------------------------------------------------ */

int obsw_s6_dump(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
{
    (void)respond;
    obsw_s6_ctx_t *s = (obsw_s6_ctx_t *)ctx;
    if (!s || !tc)
        return -1;

    if (!tc->user_data || tc->user_data_len < OBSW_S6_DUMP_LEN) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    uint32_t addr   = obsw_be32(tc->user_data);
    uint16_t length = obsw_be16(tc->user_data + 4);
    uint8_t mem_id  = tc->user_data[6];

    if (length > S6_MAX_DUMP_DATA) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
        return -1;
    }

    const uint8_t *src = s6_resolve(s, addr, length, OBSW_S6_ACCESS_READ);
    if (!src) {
        obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_MEM_ACCESS);
        return -1;
    }

    obsw_s1_accept_success(s->s1, tc);

    /* TM(6,6) data field: start_addr(4) + mem_id(1) + data */
    uint8_t buf[5U + S6_MAX_DUMP_DATA];
    buf[0] = (uint8_t)(addr >> 24);
    buf[1] = (uint8_t)(addr >> 16);
    buf[2] = (uint8_t)(addr >> 8);
    buf[3] = (uint8_t)(addr & 0xFFU);
    buf[4] = mem_id;
    memcpy(buf + 5U, src, length);

    uint16_t seq = s->msg_counter++;
    obsw_pus_tm_build(
        s->tm_store, s->apid, seq, 6, 6, seq, 0, s->timestamp, buf, (uint16_t)(5U + length));

    obsw_s1_completion_success(s->s1, tc);
    return 0;
}
