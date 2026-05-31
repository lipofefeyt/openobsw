set(OBSW_SIM_SOURCES
    main.c
    sensor_inject.c
)

if(OBSW_ENABLE_ORBITFABRIC_CONTRACT)
    list(APPEND OBSW_SIM_SOURCES orbitfabric_contract_adapter.c)
endif()

add_executable(obsw_sim ${OBSW_SIM_SOURCES})
target_link_libraries(obsw_sim PRIVATE obsw obsw_srdb_generated m)
target_include_directories(obsw_sim PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/include
)

if(OBSW_ENABLE_ORBITFABRIC_CONTRACT)
    target_compile_definitions(obsw_sim PRIVATE
        OBSW_ENABLE_ORBITFABRIC_CONTRACT=1
    )
    target_include_directories(obsw_sim PRIVATE
        ${PROJECT_SOURCE_DIR}/sim
        ${ORBITFABRIC_CONTRACT_DIR}
    )
endif()
