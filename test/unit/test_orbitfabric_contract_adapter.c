#include "unity.h"

#include "orbitfabric_contract_adapter.h"

void setUp(void) {}
void tearDown(void) {}

void test_of_cmd_ping_maps_to_tc_17_1(void)
{
    obsw_of_tc_route_t route = {0};

    TEST_ASSERT_EQUAL_INT(0, obsw_of_tc_route_for_command(OF_CMD_PING, &route));
    TEST_ASSERT_EQUAL_UINT16(0xFFFFU, route.apid);
    TEST_ASSERT_EQUAL_UINT8(17U, route.service);
    TEST_ASSERT_EQUAL_UINT8(1U, route.subservice);
}

void test_invalid_command_returns_error(void)
{
    obsw_of_tc_route_t route = {0};

    TEST_ASSERT_NOT_EQUAL(0, obsw_of_tc_route_for_command(OF_CMD_INVALID, &route));
}

void test_null_route_returns_error(void)
{
    TEST_ASSERT_NOT_EQUAL(0, obsw_of_tc_route_for_command(OF_CMD_PING, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_of_cmd_ping_maps_to_tc_17_1);
    RUN_TEST(test_invalid_command_returns_error);
    RUN_TEST(test_null_route_returns_error);
    return UNITY_END();
}
