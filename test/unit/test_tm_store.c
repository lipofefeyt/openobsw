#include "unity.h"
#include "obsw/tm/store.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_init_empty(void)
{
    obsw_tm_store_t s;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_OK, obsw_tm_store_init(&s));
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&s));
    TEST_ASSERT_EQUAL_size_t(0, obsw_tm_store_count(&s));
}

void test_enqueue_dequeue(void)
{
    obsw_tm_store_t s;
    obsw_tm_store_init(&s);

    uint8_t pkt[] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_EQUAL_INT(OBSW_TM_OK,
                          obsw_tm_store_enqueue(&s, pkt, sizeof(pkt)));
    TEST_ASSERT_EQUAL_size_t(1, obsw_tm_store_count(&s));

    uint8_t out[8];
    uint16_t len = 0;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_OK,
                          obsw_tm_store_dequeue(&s, out, sizeof(out), &len));
    TEST_ASSERT_EQUAL_UINT16(4, len);
    TEST_ASSERT_EQUAL_MEMORY(pkt, out, 4);
    TEST_ASSERT_TRUE(obsw_tm_store_empty(&s));
}

void test_fifo_order(void)
{
    obsw_tm_store_t s;
    obsw_tm_store_init(&s);

    uint8_t pkt1[] = {0x01};
    uint8_t pkt2[] = {0x02};
    obsw_tm_store_enqueue(&s, pkt1, 1);
    obsw_tm_store_enqueue(&s, pkt2, 1);

    uint8_t out[4];
    uint16_t len;
    obsw_tm_store_dequeue(&s, out, sizeof(out), &len);
    TEST_ASSERT_EQUAL_UINT8(0x01, out[0]);

    obsw_tm_store_dequeue(&s, out, sizeof(out), &len);
    TEST_ASSERT_EQUAL_UINT8(0x02, out[0]);
}

void test_dequeue_empty(void)
{
    obsw_tm_store_t s;
    obsw_tm_store_init(&s);

    uint8_t out[4];
    uint16_t len;
    TEST_ASSERT_EQUAL_INT(OBSW_TM_ERR_EMPTY,
                          obsw_tm_store_dequeue(&s, out, sizeof(out), &len));
}

void test_packet_too_large(void)
{
    obsw_tm_store_t s;
    obsw_tm_store_init(&s);

    uint8_t big[OBSW_TM_MAX_PACKET_LEN + 1];
    TEST_ASSERT_EQUAL_INT(OBSW_TM_ERR_TOO_LARGE,
                          obsw_tm_store_enqueue(&s, big, sizeof(big)));
}

void test_store_full(void)
{
    obsw_tm_store_t s;
    obsw_tm_store_init(&s);

    uint8_t pkt[] = {0xAA};
    int rc = OBSW_TM_OK;
    /* Ring buffer holds SLOTS-1 packets */
    for (size_t i = 0; i < OBSW_TM_STORE_SLOTS - 1; i++)
        rc = obsw_tm_store_enqueue(&s, pkt, 1);
    TEST_ASSERT_EQUAL_INT(OBSW_TM_OK, rc);

    TEST_ASSERT_EQUAL_INT(OBSW_TM_ERR_FULL,
                          obsw_tm_store_enqueue(&s, pkt, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_empty);
    RUN_TEST(test_enqueue_dequeue);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_dequeue_empty);
    RUN_TEST(test_packet_too_large);
    RUN_TEST(test_store_full);
    return UNITY_END();
}