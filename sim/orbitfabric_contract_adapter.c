#include "orbitfabric_contract_adapter.h"

#include <stddef.h>

int obsw_of_tc_route_for_command(of_cmd_id_t command_id,
                                 obsw_of_tc_route_t *route)
{
    if (route == NULL) {
        return -1;
    }

    switch (command_id) {
    case OF_CMD_PING:
        route->apid        = 0xFFFFU;
        route->service     = 17U;
        route->subservice  = 1U;
        return 0;

    default:
        return -1;
    }
}
