add_executable(obsw_sim main.c sensor_inject.c)
target_link_libraries(obsw_sim PRIVATE obsw obsw_srdb_generated m)
target_include_directories(obsw_sim PRIVATE ${PROJECT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR}/include)