cmake_minimum_required(VERSION 3.10)
project(SVSamplerLabSolver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ---------- 处理 JSON 库 ----------
# 假设 json 库的源码位于项目根目录的 json/ 子目录
set(JSON_BuildTests OFF CACHE INTERNAL "Disable building nlohmann_json tests")
# 注意：这里假设 json 目录是一个包含 CMakeLists.txt 的 nlohmann/json 仓库的副本
add_subdirectory(json)

# ---------- 处理 CUDD 库 ----------
# build.sh 会通过 CMAKE_PREFIX_PATH 将 ./_install 目录告知 CMake
# find_package 会自动在该路径下查找
# CUDD 库在 make install 后会生成一个 cudd.pc 文件，pkg-config 可以使用它
# 或者 CMake 的 find 模块能找到头文件和库文件。
message(STATUS "Looking for CUDD library via CMAKE_PREFIX_PATH...")

find_path(CUDD_INCLUDE_DIR cudd.h
          PATH_SUFFIXES include) # 只在 CMAKE_PREFIX_PATH 指定的路径下查找

find_library(CUDD_LIBRARY NAMES cudd
             PATH_SUFFIXES lib) # 只在 CMAKE_PREFIX_PATH 指定的路径下查找

if(CUDD_INCLUDE_DIR AND CUDD_LIBRARY)
    add_library(CUDD::cudd STATIC IMPORTED)
    set_target_properties(CUDD::cudd PROPERTIES
        IMPORTED_LOCATION "${CUDD_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CUDD_INCLUDE_DIR}")
    message(STATUS "Found CUDD in local prefix: ${CUDD_LIBRARY}")
else()
    message(FATAL_ERROR "CUDD library not found. Ensure build.sh ran correctly and installed CUDD into the project's _install directory.")
endif()


# ---------- 处理 Yosys 库 ----------
# 保持不变，因为 MySolver 不直接链接 Yosys C++ 库，只通过命令行调用其可执行文件
find_package(Yosys QUIET)
if(Yosys_FOUND)
    message(STATUS "Found Yosys package. If you need to link against it, use Yosys::yosys target or YOSYS_LIBRARIES/YOSYS_INCLUDE_DIRS if defined by the package.")
else()
    message(STATUS "Yosys package not found by find_package. This is usually fine if you only use the 'yosys' command-line tool (ensure it's in your PATH). If you intend to link against Yosys C++ libraries, ensure they are installed with development files and CMake can find them (e.g., via CMAKE_PREFIX_PATH or by setting YOSYS_DIR).")
endif()


# ---------- 构建 MySolver ----------
add_executable(MySolver src/main.cpp src/json_v_converter.cpp src/aig_bdd_solver.cpp)

# 链接 MySolver 需要的库
# nlohmann_json::nlohmann_json 是由 add_subdirectory(json) 创建的目标
target_link_libraries(MySolver PRIVATE nlohmann_json::nlohmann_json)
# CUDD::cudd 是我们上面创建的 IMPORTED 目标
target_link_libraries(MySolver PRIVATE CUDD::cudd)
target_link_libraries(MySolver PRIVATE quadmath) # 假设 quadmath 是系统库，不需要特殊处理

message(STATUS "CMake configuration done. To build, run 'make' or your chosen build tool.")