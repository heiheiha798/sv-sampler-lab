cmake_minimum_required(VERSION 3.10)
project(SVSamplerLabSolver)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找 nlohmann_json
# 将 nlohmann_json 作为子目录添加 (假设它在 ./json/ 目录下)
# 这将从 json/CMakeLists.txt 构建 nlohmann_json 并使其目标可用
set(JSON_BuildTests OFF CACHE INTERNAL "Disable building nlohmann_json tests")
add_subdirectory(json) # 假设 nlohmann_json 的源代码在 'json' 子目录中
# add_subdirectory 会定义 nlohmann_json::nlohmann_json 目标

# 查找 CUDD
# 选项 1: CUDD 提供 CMake 配置文件 (对于较旧的 C 项目不太常见)
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
              PATH_SUFFIXES include include/cudd) # cudd.h 可能在 include/ 或 include/cudd/
    find_library(CUDD_LIBRARY NAMES cudd
                 HINTS ENV CUDD_DIR /usr/local /usr
                 PATH_SUFFIXES lib lib64)

    if(CUDD_INCLUDE_DIR AND CUDD_LIBRARY)
        add_library(CUDD::cudd UNKNOWN IMPORTED)
        set_target_properties(CUDD::cudd PROPERTIES
                              IMPORTED_LOCATION "${CUDD_LIBRARY}"
                              INTERFACE_INCLUDE_DIRECTORIES "${CUDD_INCLUDE_DIR}")
        message(STATUS "Found CUDD manually: ${CUDD_LIBRARY} (include: ${CUDD_INCLUDE_DIR})")
    else()
        message(FATAL_ERROR "CUDD library or headers not found. Searched CUDD_INCLUDE_DIR=${CUDD_INCLUDE_DIR}, CUDD_LIBRARY=${CUDD_LIBRARY}. Please ensure CUDD is installed (e.g., in /usr/local) or set CUDD_DIR environment variable.")
    endif()
endif()

# 查找 Yosys (主要用于信息，或如果需要链接特定的 Yosys 库)
# Yosys 通常作为命令行工具使用。您的 build.sh 或 run.sh 可能会调用它。
# 如果您需要将 C++ 代码链接到 Yosys 库:
find_package(Yosys QUIET)
if(Yosys_FOUND)
    message(STATUS "Found Yosys package. If you need to link against it, use Yosys::yosys target or YOSYS_LIBRARIES/YOSYS_INCLUDE_DIRS if defined by the package.")
    # 如果 Yosys 包定义了目标或变量，并且您需要链接，可以在此处添加链接逻辑
    # 例如:
    # if(TARGET Yosys::yosys)
    #   target_link_libraries(MySolver PRIVATE Yosys::yosys)
    #   message(STATUS "Linking MySolver against Yosys target.")
    # elseif(YOSYS_LIBRARIES AND YOSYS_INCLUDE_DIRS)
    #   target_include_directories(MySolver PRIVATE ${YOSYS_INCLUDE_DIRS})
    #   target_link_libraries(MySolver PRIVATE ${YOSYS_LIBRARIES})
    #   message(STATUS "Linking MySolver against Yosys libraries (manual).")
    # endif()
else()
    message(STATUS "Yosys package not found by find_package. This is usually fine if you only use the 'yosys' command-line tool (ensure it's in your PATH). If you intend to link against Yosys C++ libraries, ensure they are installed with development files and CMake can find them (e.g., via CMAKE_PREFIX_PATH or by setting YOSYS_DIR).")
endif()

# 添加您的可执行文件
# add_executable 命令定义了一个名为 MySolver 的可执行目标，
# 并指定 src/main.cpp (以及此处列出的任何其他 .cpp 文件) 作为其源文件。
# C/C++ 编译器和链接器随后会在这些源文件中查找 main() 函数作为程序的入口点。
add_executable(MySolver src/main.cpp)

# 链接库
target_link_libraries(MySolver PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(MySolver PRIVATE CUDD::cudd)

# 如果需要，可以添加其他编译选项或包含目录
# target_include_directories(MySolver PRIVATE ${SOME_OTHER_INCLUDE_DIR})

message(STATUS "CMake configuration done. To build, run 'make' or your chosen build tool.")
