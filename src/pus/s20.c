/**
 * @file s20.c
 * @brief PUS-C Service 20 — On-Board Parameter Management.
 */

 #include "obsw/pus/s20.h"
 #include "obsw/util/bytes.h"

 /* TC(20,1) user data: param_id(2 BE) | value(4 BE) */
 #define S20_SET_LEN 6U
 
 /* TC(20,3) user data: param_id(2 BE) */
 #define S20_GET_LEN 2U
 
 /* ------------------------------------------------------------------ */
 /* Internal helpers                                                    */
 /* ------------------------------------------------------------------ */
 
 /** Find a parameter by ID in the parameter table; returns NULL if not found. */
 static obsw_s20_param_t *find_param(obsw_s20_ctx_t *s, uint16_t id)
 {
     for (uint8_t i = 0; i < s->table_len; i++) {
         if (s->table[i].param_id == id) {
             return &s->table[i];
         }
     }
     return NULL;
 }
 
 /** Emit a TM(20,2) parameter value report for the given param_id and value. */
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
 
     uint16_t seq = s->msg_counter;
 
     s->msg_counter++;
     (void)obsw_pus_tm_build(s->tm_store, s->apid, seq,
                       20U, 2U, seq, 0U, s->timestamp,
                       body, 6U);
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,1) — set                                                      */
 /* ------------------------------------------------------------------ */
 
 int obsw_s20_set(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
 {
     (void)&respond; /* unused: handlers report through S1 */
     obsw_s20_ctx_t *s = (obsw_s20_ctx_t *)ctx;
     if (!s || !tc) {
         return -1;
     }
 
     if (!tc->user_data || (tc->user_data_len < S20_SET_LEN)) {
         (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
         return -1;
     }
 
     uint16_t param_id = obsw_be16(tc->user_data);
     obsw_s20_value_t val;
     val.u32 = obsw_be32(tc->user_data + 2);
 
     obsw_s20_param_t *p = find_param(s, param_id);
     if (!p) {
         (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
         return -1;
     }
 
     (void)obsw_s1_accept_success(s->s1, tc);
     p->value = val;
     (void)obsw_s1_completion_success(s->s1, tc);
     return 0;
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,3) — get                                                      */
 /* ------------------------------------------------------------------ */
 
 int obsw_s20_get(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx)
 {
     (void)&respond; /* unused: handlers report through S1 */
     obsw_s20_ctx_t *s = (obsw_s20_ctx_t *)ctx;
     if (!s || !tc) {
         return -1;
     }
 
     if (!tc->user_data || (tc->user_data_len < S20_GET_LEN)) {
         (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_ILLEGAL_APP_DATA);
         return -1;
     }
 
     uint16_t param_id = obsw_be16(tc->user_data);

     obsw_s20_param_t *p = find_param(s, param_id);
     if (!p) {
         (void)obsw_s1_accept_failure(s->s1, tc, OBSW_S1_FAIL_EXEC_ERROR);
         return -1;
     }
 
     (void)obsw_s1_accept_success(s->s1, tc);
     emit_tm20_2(s, param_id, p->value);
     (void)obsw_s1_completion_success(s->s1, tc);
     return 0;
 }

 const obsw_s20_param_t *obsw_s20_find(const obsw_s20_param_t *table,
                                       uint8_t table_len,
                                       uint16_t param_id)
 {
     if (table == NULL) {
         return NULL;
     }
     for (uint8_t i = 0; i < table_len; i++) {
         if (table[i].param_id == param_id) {
             return &table[i];
         }
     }
     return NULL;
 }

 float obsw_s20_as_float(const obsw_s20_param_t *p)
 {
     if (p->type == OBSW_S20_F32) {
         return p->value.f32;
     }
     if (p->type == OBSW_S20_I32) {
         return (float)p->value.i32;
     }
     return (float)p->value.u32;
 }
