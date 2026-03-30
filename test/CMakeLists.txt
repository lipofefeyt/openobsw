include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.5.2
)
FetchContent_MakeAvailable(unity)

set(TEST_SUPPRESS -Wno-missing-prototypes)

# v0.1 — core
add_executable(test_space_packet unit/test_space_packet.c)
target_link_libraries(test_space_packet PRIVATE obsw unity)
target_compile_options(test_space_packet PRIVATE ${TEST_SUPPRESS})
add_test(NAME space_packet COMMAND test_space_packet)

add_executable(test_dispatcher unit/test_dispatcher.c)
target_link_libraries(test_dispatcher PRIVATE obsw unity)
target_compile_options(test_dispatcher PRIVATE ${TEST_SUPPRESS})
add_test(NAME dispatcher COMMAND test_dispatcher)

add_executable(test_tm_store unit/test_tm_store.c)
target_link_libraries(test_tm_store PRIVATE obsw unity)
target_compile_options(test_tm_store PRIVATE ${TEST_SUPPRESS})
add_test(NAME tm_store COMMAND test_tm_store)

# v0.2 — framing
add_executable(test_tc_frame unit/test_tc_frame.c)
target_link_libraries(test_tc_frame PRIVATE obsw unity)
target_compile_options(test_tc_frame PRIVATE ${TEST_SUPPRESS})
add_test(NAME tc_frame COMMAND test_tc_frame)

add_executable(test_tm_frame unit/test_tm_frame.c)
target_link_libraries(test_tm_frame PRIVATE obsw unity)
target_compile_options(test_tm_frame PRIVATE ${TEST_SUPPRESS})
add_test(NAME tm_frame COMMAND test_tm_frame)

# v0.3 — PUS-C services
add_executable(test_s1 unit/test_s1.c)
target_link_libraries(test_s1 PRIVATE obsw unity)
target_compile_options(test_s1 PRIVATE ${TEST_SUPPRESS})
add_test(NAME s1 COMMAND test_s1)

add_executable(test_s3 unit/test_s3.c)
target_link_libraries(test_s3 PRIVATE obsw unity)
target_compile_options(test_s3 PRIVATE ${TEST_SUPPRESS})
add_test(NAME s3 COMMAND test_s3)

add_executable(test_s5 unit/test_s5.c)
target_link_libraries(test_s5 PRIVATE obsw unity)
target_compile_options(test_s5 PRIVATE ${TEST_SUPPRESS})
add_test(NAME s5 COMMAND test_s5)

add_executable(test_s17 unit/test_s17.c)
target_link_libraries(test_s17 PRIVATE obsw unity)
target_compile_options(test_s17 PRIVATE ${TEST_SUPPRESS})
add_test(NAME s17 COMMAND test_s17)

# v0.4 — FDIR
add_executable(test_fsm unit/test_fsm.c)
target_link_libraries(test_fsm PRIVATE obsw unity)
target_compile_options(test_fsm PRIVATE ${TEST_SUPPRESS})
add_test(NAME fsm COMMAND test_fsm)

add_executable(test_watchdog unit/test_watchdog.c)
target_link_libraries(test_watchdog PRIVATE obsw unity)
target_compile_options(test_watchdog PRIVATE ${TEST_SUPPRESS})
add_test(NAME watchdog COMMAND test_watchdog)

add_executable(test_s5_fdir unit/test_s5_fdir.c)
target_link_libraries(test_s5_fdir PRIVATE obsw unity)
target_compile_options(test_s5_fdir PRIVATE ${TEST_SUPPRESS})
add_test(NAME s5_fdir COMMAND test_s5_fdir)

# Integration
add_executable(test_tc_pipeline integration/test_tc_pipeline.c)
target_link_libraries(test_tc_pipeline PRIVATE obsw unity)
target_compile_options(test_tc_pipeline PRIVATE ${TEST_SUPPRESS})
add_test(NAME tc_pipeline COMMAND test_tc_pipeline)