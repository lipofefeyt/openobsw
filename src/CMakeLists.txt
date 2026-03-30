add_library(obsw STATIC
    ccsds/space_packet.c
    ccsds/tc_frame.c
    ccsds/tm_frame.c
    tc/dispatcher.c
    tm/store.c
)

target_include_directories(obsw PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

set_target_properties(obsw PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)