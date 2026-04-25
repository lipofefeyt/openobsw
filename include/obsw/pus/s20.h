/**
 * @file s20.h
 * @brief PUS-C Service 20 — On-Board Parameter Management.
 *
 * Provides get and set access to a static, caller-owned parameter table.
 * Each parameter is a 32-bit value (union of uint32/int32/float32) with
 * a 16-bit ID.  Zero dynamic allocation.
 *
 * TC handlers registered in the dispatcher routing table:
 *   TC(20,1)  Parameter value set  → obsw_s20_set()
 *   TC(20,3)  Parameter value get  → obsw_s20_get()
 *
 * TC(20,1) user data layout (one parameter per TC):
 *   param_id (2 bytes BE) | raw_value (4 bytes BE)
 *
 * TC(20,3) user data layout:
 *   param_id (2 bytes BE)
 *
 * TM(20,2) parameter value report layout (response to TC(20,3)):
 *   param_id (2 bytes BE) | raw_value (4 bytes BE)
 */
 #ifndef OBSW_PUS_S20_H
 #define OBSW_PUS_S20_H
 
 #include "obsw/pus/pus_tm.h"
 #include "obsw/pus/s1.h"
 #include "obsw/tc/dispatcher.h"
 
 #include <stdint.h>
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /* ------------------------------------------------------------------ */
 /* Parameter entry                                                     */
 /* ------------------------------------------------------------------ */
 
 /** Raw 32-bit parameter value — caller interprets as uint32/int32/float. */
 typedef union {
     uint32_t u32;
     int32_t  i32;
     float    f32;
 } obsw_s20_value_t;
 
 /** Single entry in the static parameter table. */
 typedef struct {
     uint16_t       param_id;
     obsw_s20_value_t value;
 } obsw_s20_param_t;
 
 /* ------------------------------------------------------------------ */
 /* Context                                                             */
 /* ------------------------------------------------------------------ */
 
 typedef struct {
     obsw_tm_store_t   *tm_store;
     obsw_s1_ctx_t     *s1;
     uint16_t           apid;
     uint16_t           msg_counter;
     uint32_t           timestamp;
     obsw_s20_param_t  *table;     /**< Caller-owned static parameter table */
     uint8_t            table_len;
 } obsw_s20_ctx_t;
 
 /* ------------------------------------------------------------------ */
 /* TC handlers                                                         */
 /* ------------------------------------------------------------------ */
 
 /**
  * TC(20,1) — set parameter value.
  * Looks up param_id in the table and overwrites the stored value.
  * Emits TM(1,1) acceptance and TM(1,7) completion on success,
  * or TM(1,2) / TM(1,8) on failure.
  */
 int obsw_s20_set(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);
 
 /**
  * TC(20,3) — get parameter value.
  * Looks up param_id and emits TM(20,2) carrying the current raw value.
  * Emits TM(1,1) acceptance and TM(1,7) completion on success,
  * or TM(1,2) on unknown param_id.
  */
 int obsw_s20_get(const obsw_tc_t *tc, obsw_tc_responder_t respond, void *ctx);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* OBSW_PUS_S20_H */