#ifndef OBSW_SIM_ORBITFABRIC_CONTRACT_ADAPTER_H
#define OBSW_SIM_ORBITFABRIC_CONTRACT_ADAPTER_H

#include "mission_contract.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t apid;
    uint8_t service;
    uint8_t subservice;
} obsw_of_tc_route_t;

int obsw_of_tc_route_for_command(of_cmd_id_t command_id,
                                 obsw_of_tc_route_t *route);

#ifdef __cplusplus
}
#endif

#endif /* OBSW_SIM_ORBITFABRIC_CONTRACT_ADAPTER_H */
