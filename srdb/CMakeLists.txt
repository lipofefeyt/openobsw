# srdb/CMakeLists.txt — SRDB code generation
#
# Runs codegen.py directly via PYTHONPATH — no pip install required at
# build time. The codegen script and its dependencies (pydantic, pyyaml)
# must be importable from the active Python environment.
#
# For OpenSVF integration, install the package once manually:
#   pip install -e srdb/
#
# The generated header is exposed as an INTERFACE target:
#   target_link_libraries(my_target PRIVATE obsw_srdb_generated)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(SRDB_PKG_DIR    "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRDB_DATA_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/data")
set(SRDB_GENERATED_DIR "${CMAKE_BINARY_DIR}/include/obsw")
set(SRDB_GENERATED_H   "${SRDB_GENERATED_DIR}/srdb_generated.h")

file(GLOB SRDB_YAML_FILES "${SRDB_DATA_DIR}/*.yaml")

add_custom_command(
    OUTPUT "${SRDB_GENERATED_H}"
    COMMAND "${CMAKE_COMMAND}" -E env "PYTHONPATH=${SRDB_PKG_DIR}"
            "${Python3_EXECUTABLE}" -m obsw_srdb.codegen
            --data-dir "${SRDB_DATA_DIR}"
            --output   "${SRDB_GENERATED_H}"
    DEPENDS
        ${SRDB_YAML_FILES}
        "${SRDB_PKG_DIR}/obsw_srdb/codegen.py"
        "${SRDB_PKG_DIR}/obsw_srdb/loader.py"
        "${SRDB_PKG_DIR}/obsw_srdb/model.py"
    COMMENT "[srdb] Generating srdb_generated.h"
    VERBATIM
)

add_custom_target(srdb_codegen ALL
    DEPENDS "${SRDB_GENERATED_H}"
)

add_library(obsw_srdb_generated INTERFACE)
add_dependencies(obsw_srdb_generated srdb_codegen)
target_include_directories(obsw_srdb_generated INTERFACE
    "${CMAKE_BINARY_DIR}/include"
)