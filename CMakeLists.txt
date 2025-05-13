cmake_minimum_required(VERSION 3.10)
project(SVSamplerLabSolver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(JSON_BuildTests OFF CACHE INTERNAL "Disable building nlohmann_json tests")
add_subdirectory(json) 

find_package(CUDD QUIET)
if(CUDD_FOUND AND TARGET CUDD::cudd)
    message(STATUS "Found CUDD via find_package (target CUDD::cudd).")
elif(CUDD_FOUND AND CUDD_LIBRARIES AND CUDD_INCLUDE_DIRS)
    message(STATUS "Found CUDD via find_package (variables set).")
    add_library(CUDD::cudd INTERFACE IMPORTED)
    target_link_libraries(CUDD::cudd INTERFACE ${CUDD_LIBRARIES})
    target_include_directories(CUDD::cudd INTERFACE ${CUDD_INCLUDE_DIRS})
    message(STATUS "Created imported target CUDD::cudd using CUDD_LIBRARIES and CUDD_INCLUDE_DIRS.")
else()
    message(STATUS "CUDD package not found by find_package, trying to find manually.")
    find_path(CUDD_INCLUDE_DIR cudd.h
            HINTS ENV CUDD_DIR /usr/local /usr
            PATH_SUFFIXES include include/cudd)
    find_library(CUDD_LIBRARY NAMES cudd.a cudd
                HINTS ENV CUDD_DIR /usr/local /usr
                PATH_SUFFIXES lib lib64)

    if(CUDD_INCLUDE_DIR AND CUDD_LIBRARY)
        add_library(CUDD::cudd STATIC IMPORTED)
        set_target_properties(CUDD::cudd PROPERTIES
                                IMPORTED_LOCATION "${CUDD_LIBRARY}"
                                INTERFACE_INCLUDE_DIRECTORIES "${CUDD_INCLUDE_DIR}")
        message(STATUS "Found CUDD manually: ${CUDD_LIBRARY} (include: ${CUDD_INCLUDE_DIR})")
    else()
        message(FATAL_ERROR "CUDD library or headers not found. Searched CUDD_INCLUDE_DIR=${CUDD_INCLUDE_DIR}, CUDD_LIBRARY=${CUDD_LIBRARY}. Please ensure CUDD is installed (e.g., in /usr/local) or set CUDD_DIR environment variable.")
    endif()
endif()

find_package(Yosys QUIET)
if(Yosys_FOUND)
    message(STATUS "Found Yosys package. If you need to link against it, use Yosys::yosys target or YOSYS_LIBRARIES/YOSYS_INCLUDE_DIRS if defined by the package.")
else()
    message(STATUS "Yosys package not found by find_package. This is usually fine if you only use the 'yosys' command-line tool (ensure it's in your PATH). If you intend to link against Yosys C++ libraries, ensure they are installed with development files and CMake can find them (e.g., via CMAKE_PREFIX_PATH or by setting YOSYS_DIR).")
endif()

add_executable(MySolver src/main.cpp src/json_v_converter.cpp src/aig_bdd_solver.cpp)

target_link_libraries(MySolver PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(MySolver PRIVATE CUDD::cudd)
target_link_libraries(MySolver PRIVATE quadmath)

message(STATUS "CMake configuration done. To build, run 'make' or your chosen build tool.")