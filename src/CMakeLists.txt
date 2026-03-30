add_library(obsw STATIC
    ccsds/space_packet.c
    ccsds/tc_frame.c
    ccsds/tm_frame.c
    tc/dispatcher.c
    tm/store.c
    pus/pus_tm.c
    pus/s1.c
    pus/s3.c
    pus/s5.c
    pus/s17.c
    fdir/fsm.c
    fdir/watchdog.c
)

target_include_directories(obsw PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(obsw PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)