/**
 * @file s20.c
 * @brief PUS-C Service 20 — On-Board Parameter Management.
 */

 #include "obsw/pus/s20.h"

 /* TC(20,1) user data: param_id(2 BE) | value(4 BE) */
 #define S20_SET_LEN 6U
 
 /* TC(20,3) user data: param_id(2 BE) */
 #define S20_GET_LEN 2U
 
 /* ------------------------------------------------------------------ */
 /* Internal helpers                                                    */
 /* ------------------------------------------------------------------ */
 
 static obsw_s20_param_t *find_param(obsw_s20_ctx_t *s, uint16_t id)
 {
     for (uint8_t i = 0; i < s->table_len; i++) {
         if (s->table[i].param_id == id)
             return &s->table[i];
     }
     return NULL;
 }
 
 static void emit_tm20_2(obsw_s20_ctx_t *s,
                         uint16_t param_id,
                         obsw_s20_value_t val)
 {
     uint8_t body[6];
     body[0] = (uint8_t)(param_id >> 8);
     body[1] = (uint8_t)(param_id & 0xFFU);
     body[2] = (uint8_t)(val.u32 >> 24);
     body[3] = (uint8_t)(val.u32 >> 16);
     body[4] = (uint8_t)(val.u32 >>  8);
     body[5] = (uint8_t)(val.u32 & 0xFFU);
 
     uint16_t seq = s->msg_counter++;
     obsw_pus_tm_build(s->tm_store, s->apid, seq,
                       20U, 2U, seq, 0U, s->timestamp,
                       body, 6U);
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,1) — set                                                      */
 /* ------------------------------------------------------------------ */
 
 int obsw_s20_set(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
 {
     (void)respond;
     obsw_s20_ctx_t *s = (obsw_s20_ctx_t *)ctx;
     if (!s || !tc)
         return -1;
 
     if (!tc->user_data || tc->user_data_len < S20_SET_LEN) {
         obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_SUBSVC);
         return -1;
     }
 
     uint16_t param_id = (uint16_t)(((uint16_t)tc->user_data[0] << 8)
                                    | tc->user_data[1]);
     obsw_s20_value_t val;
     val.u32 = ((uint32_t)tc->user_data[2] << 24)
             | ((uint32_t)tc->user_data[3] << 16)
             | ((uint32_t)tc->user_data[4] <<  8)
             |  (uint32_t)tc->user_data[5];
 
     obsw_s20_param_t *p = find_param(s, param_id);
     if (!p) {
         obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
         return -1;
     }
 
     obsw_s1_accept_success(s->s1, tc);
     p->value = val;
     obsw_s1_completion_success(s->s1, tc);
     return 0;
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,3) — get                                                      */
 /* ------------------------------------------------------------------ */
 
 int obsw_s20_get(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
 {
     (void)respond;
     obsw_s20_ctx_t *s = (obsw_s20_ctx_t *)ctx;
     if (!s || !tc)
         return -1;
 
     if (!tc->user_data || tc->user_data_len < S20_GET_LEN) {
         obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_SUBSVC);
         return -1;
     }
 
     uint16_t param_id = (uint16_t)(((uint16_t)tc->user_data[0] << 8)
                                    | tc->user_data[1]);
 
     obsw_s20_param_t *p = find_param(s, param_id);
     if (!p) {
         obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
         return -1;
     }
 
     obsw_s1_accept_success(s->s1, tc);
     emit_tm20_2(s, param_id, p->value);
     obsw_s1_completion_success(s->s1, tc);
     return 0;
 }