add_executable(obsw_sim main.c)
target_link_libraries(obsw_sim PRIVATE obsw)
target_include_directories(obsw_sim PRIVATE ${PROJECT_SOURCE_DIR}/include)
