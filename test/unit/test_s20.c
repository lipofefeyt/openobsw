/**
 * @file test_s20.c
 * @brief Unit tests for PUS-C Service 20 — On-Board Parameter Management.
 *
 * Tests TC(20,1) set and TC(20,3) get against a static parameter table.
 */

 #include "obsw/ccsds/space_packet.h"
 #include "obsw/pus/s20.h"
 #include "obsw/tm/store.h"
 #include "unity.h"
 
 #include <string.h>
 
 void setUp(void) {}
 void tearDown(void) {}
 
 /* ------------------------------------------------------------------ */
 /* Fixtures                                                            */
 /* ------------------------------------------------------------------ */
 
 static obsw_tm_store_t store;
 static obsw_s1_ctx_t   s1;
 static obsw_s20_ctx_t  s20;
 
 /* Three test parameters:
  *   0x0001 — uint32 counter (starts 0)
  *   0x0004 — uptime (starts 42)
  *   0x0010 — float gain (starts 1.0e4 as uint32 bit pattern)
  */
 static obsw_s20_param_t table[3];
 
 static void setup_ctx(void)
 {
     obsw_tm_store_init(&store);
 
     s1.tm_store    = &store;
     s1.apid        = 0x010;
     s1.msg_counter = 0;
     s1.timestamp   = 0;
 
     table[0].param_id    = 0x0001;
     table[0].value.u32   = 0U;
     table[1].param_id    = 0x0004;
     table[1].value.u32   = 42U;
     table[2].param_id    = 0x0010;
     table[2].value.f32   = 1.0e4f;
 
     s20.tm_store    = &store;
     s20.s1          = &s1;
     s20.apid        = 0x010;
     s20.msg_counter = 0;
     s20.timestamp   = 0;
     s20.table       = table;
     s20.table_len   = 3;
 }
 
 /* Build TC(20,1) set: param_id + 4-byte BE value */
 static obsw_tc_t make_set_tc(uint16_t param_id, uint32_t raw, uint8_t *ud)
 {
     ud[0] = (uint8_t)(param_id >> 8);
     ud[1] = (uint8_t)(param_id & 0xFFU);
     ud[2] = (uint8_t)(raw >> 24);
     ud[3] = (uint8_t)(raw >> 16);
     ud[4] = (uint8_t)(raw >>  8);
     ud[5] = (uint8_t)(raw & 0xFFU);
     obsw_tc_t tc = {
         .apid          = 0x010,
         .service       = 20,
         .subservice    = 1,
         .seq_count     = 1,
         .user_data     = ud,
         .user_data_len = 6,
     };
     return tc;
 }
 
 /* Build TC(20,3) get: param_id only */
 static obsw_tc_t make_get_tc(uint16_t param_id, uint8_t *ud)
 {
     ud[0] = (uint8_t)(param_id >> 8);
     ud[1] = (uint8_t)(param_id & 0xFFU);
     obsw_tc_t tc = {
         .apid          = 0x010,
         .service       = 20,
         .subservice    = 3,
         .seq_count     = 1,
         .user_data     = ud,
         .user_data_len = 2,
     };
     return tc;
 }
 
 /* Dequeue one packet and parse it */
 static obsw_sp_packet_t dequeue_pkt(uint8_t *buf, size_t buf_len)
 {
     uint16_t len = 0;
     obsw_tm_store_dequeue(&store, buf, (uint16_t)buf_len, &len);
     obsw_sp_packet_t pkt = {0};
     obsw_sp_parse(buf, len, &pkt);
     return pkt;
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,1) — set tests                                                */
 /* ------------------------------------------------------------------ */
 
 void test_set_known_param_returns_ok(void)
 {
     setup_ctx();
     uint8_t ud[6];
     obsw_tc_t tc = make_set_tc(0x0001, 0xDEADBEEFU, ud);
     TEST_ASSERT_EQUAL_INT(0, obsw_s20_set(&tc, NULL, &s20));
     TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, table[0].value.u32);
 }
 
 void test_set_emits_tm11_and_tm17(void)
 {
     setup_ctx();
     uint8_t ud[6];
     obsw_tc_t tc = make_set_tc(0x0004, 100U, ud);
     obsw_s20_set(&tc, NULL, &s20);
 
     TEST_ASSERT_EQUAL_size_t(2, obsw_tm_store_count(&store));
 
     uint8_t buf[256];
     obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc=1 */
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]); /* subsvc=1 — accept */
 
     pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]); /* svc=1 */
     TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]); /* subsvc=7 — complete */
 }
 
 void test_set_unknown_param_emits_tm12(void)
 {
     setup_ctx();
     uint8_t ud[6];
     obsw_tc_t tc = make_set_tc(0xBEEF, 0U, ud);
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_set(&tc, NULL, &s20));
 
     uint8_t buf[256];
     obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
     TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]); /* TM(1,2) — accept fail */
 }
 
 void test_set_truncated_ud_emits_tm12(void)
 {
     setup_ctx();
     uint8_t ud[3] = {0x00, 0x01, 0x00};
     obsw_tc_t tc = {
         .apid = 0x010, .service = 20, .subservice = 1,
         .seq_count = 1, .user_data = ud, .user_data_len = 3,
     };
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_set(&tc, NULL, &s20));
     TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
 }
 
 void test_set_null_ctx_returns_error(void)
 {
     setup_ctx();
     uint8_t ud[6];
     obsw_tc_t tc = make_set_tc(0x0001, 0U, ud);
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_set(&tc, NULL, NULL));
 }
 
 /* ------------------------------------------------------------------ */
 /* TC(20,3) — get tests                                                */
 /* ------------------------------------------------------------------ */
 
 void test_get_known_param_returns_ok(void)
 {
     setup_ctx();
     uint8_t ud[2];
     obsw_tc_t tc = make_get_tc(0x0004, ud);
     TEST_ASSERT_EQUAL_INT(0, obsw_s20_get(&tc, NULL, &s20));
 }
 
 void test_get_emits_tm11_tm20_2_tm17(void)
 {
     setup_ctx();
     uint8_t ud[2];
     obsw_tc_t tc = make_get_tc(0x0004, ud); /* uptime = 42 */
     obsw_s20_get(&tc, NULL, &s20);
 
     /* TM(1,1) + TM(20,2) + TM(1,7) = 3 packets */
     TEST_ASSERT_EQUAL_size_t(3, obsw_tm_store_count(&store));
 
     uint8_t buf[256];
     obsw_sp_packet_t pkt;
 
     /* TM(1,1) */
     pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[2]);
 
     /* TM(20,2) — check param_id and value */
     pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(20, pkt.payload[1]); /* svc=20 */
     TEST_ASSERT_EQUAL_UINT8(2,  pkt.payload[2]); /* subsvc=2 */
     /* Payload after secondary header: param_id(2) | value(4) */
     /* PUS-C secondary header is 10 bytes in TM, so app data starts at offset 10 */
     uint8_t *app = pkt.payload + 11; /* after PUS-C TM secondary header (11 bytes) */
     uint16_t reported_id = (uint16_t)(((uint16_t)app[0] << 8) | app[1]);
     uint32_t reported_val = ((uint32_t)app[2] << 24) | ((uint32_t)app[3] << 16)
                           | ((uint32_t)app[4] <<  8) |  (uint32_t)app[5];
     TEST_ASSERT_EQUAL_UINT16(0x0004, reported_id);
     TEST_ASSERT_EQUAL_UINT32(42U, reported_val);
 
     /* TM(1,7) */
     pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
     TEST_ASSERT_EQUAL_UINT8(7, pkt.payload[2]);
 }
 
 void test_get_unknown_param_emits_tm12(void)
 {
     setup_ctx();
     uint8_t ud[2];
     obsw_tc_t tc = make_get_tc(0xBEEF, ud);
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_get(&tc, NULL, &s20));
 
     uint8_t buf[256];
     obsw_sp_packet_t pkt = dequeue_pkt(buf, sizeof(buf));
     TEST_ASSERT_EQUAL_UINT8(1, pkt.payload[1]);
     TEST_ASSERT_EQUAL_UINT8(2, pkt.payload[2]); /* TM(1,2) */
 }
 
 void test_get_truncated_ud_emits_tm12(void)
 {
     setup_ctx();
     uint8_t ud[1] = {0x00};
     obsw_tc_t tc = {
         .apid = 0x010, .service = 20, .subservice = 3,
         .seq_count = 1, .user_data = ud, .user_data_len = 1,
     };
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_get(&tc, NULL, &s20));
     TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&store));
 }
 
 void test_get_null_ctx_returns_error(void)
 {
     setup_ctx();
     uint8_t ud[2];
     obsw_tc_t tc = make_get_tc(0x0001, ud);
     TEST_ASSERT_NOT_EQUAL(0, obsw_s20_get(&tc, NULL, NULL));
 }
 
 /* ------------------------------------------------------------------ */
 /* Round-trip: set then get                                            */
 /* ------------------------------------------------------------------ */
 
 void test_set_then_get_roundtrip(void)
 {
     setup_ctx();
     uint8_t ud[6];
 
     /* Set 0x0001 to 0xCAFEBABE */
     obsw_tc_t set_tc = make_set_tc(0x0001, 0xCAFEBABEU, ud);
     TEST_ASSERT_EQUAL_INT(0, obsw_s20_set(&set_tc, NULL, &s20));
 
     /* Value immediately visible in the parameter table */
     TEST_ASSERT_EQUAL_UINT32(0xCAFEBABEU, table[0].value.u32);
 
     /* Get returns OK and produces TM(1,1) + TM(20,2) + TM(1,7) */
     obsw_tm_store_init(&store);
     uint8_t ud2[2];
     obsw_tc_t get_tc = make_get_tc(0x0001, ud2);
     TEST_ASSERT_EQUAL_INT(0, obsw_s20_get(&get_tc, NULL, &s20));
     TEST_ASSERT_EQUAL_size_t(3, obsw_tm_store_count(&store));
 }
 
 /* ------------------------------------------------------------------ */
 /* Main                                                                */
 /* ------------------------------------------------------------------ */
 
 int main(void)
 {
     UNITY_BEGIN();
 
     /* TC(20,1) — set */
     RUN_TEST(test_set_known_param_returns_ok);
     RUN_TEST(test_set_emits_tm11_and_tm17);
     RUN_TEST(test_set_unknown_param_emits_tm12);
     RUN_TEST(test_set_truncated_ud_emits_tm12);
     RUN_TEST(test_set_null_ctx_returns_error);
 
     /* TC(20,3) — get */
     RUN_TEST(test_get_known_param_returns_ok);
     RUN_TEST(test_get_emits_tm11_tm20_2_tm17);
     RUN_TEST(test_get_unknown_param_emits_tm12);
     RUN_TEST(test_get_truncated_ud_emits_tm12);
     RUN_TEST(test_get_null_ctx_returns_error);
 
     /* Round-trip */
     RUN_TEST(test_set_then_get_roundtrip);
 
     return UNITY_END();
 }