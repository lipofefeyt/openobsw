include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.5.2
)
FetchContent_MakeAvailable(unity)

set(TEST_SUPPRESS -Wno-missing-prototypes)

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
