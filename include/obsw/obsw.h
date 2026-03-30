/**
 * @file obsw.h
 * @brief openobsw — top-level include.
 */
#ifndef OBSW_H
#define OBSW_H

#include "obsw/ccsds/space_packet.h"
#include "obsw/ccsds/tc_frame.h"
#include "obsw/ccsds/tm_frame.h"
#include "obsw/tc/dispatcher.h"
#include "obsw/tm/store.h"
#include "obsw/hal/io.h"
#include "obsw/pus/pus_tm.h"
#include "obsw/pus/s1.h"
#include "obsw/pus/s3.h"
#include "obsw/pus/s5.h"
#include "obsw/pus/s17.h"

#define OBSW_VERSION_MAJOR 0
#define OBSW_VERSION_MINOR 3
#define OBSW_VERSION_PATCH 0

#endif /* OBSW_H */