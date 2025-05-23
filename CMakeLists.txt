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
# 直接将本地的 cudd 目录作为子目录添加，而不是通过 find_package 查找系统安装的 CUDD
# 这假设 cudd/ 目录包含 CUDD 库的源代码和 CMakeLists.txt 或 configure 脚本
# 如果 cudd/ 目录没有 CMakeLists.txt，你需要根据其构建方式来添加（例如，如果它只提供 .h 和 .a 文件，则需要手动添加库）
# 鉴于你的 build.sh 脚本会进入 cudd/ 目录进行 autoreconf, configure, make install
# 这表明 cudd 是一个独立的构建项目，我们不应该在这里 add_subdirectory。
# 相反，我们应该链接到 build.sh 编译并安装到 /usr/local 的 CUDD 库。
# 所以，我们保留 CUDD 的 find 逻辑，但优化搜索路径。

# 移除 find_package 的所有逻辑，直接查找本地安装的 CUDD 库
# 因为 build.sh 已经 sudo make install 到 /usr/local
message(STATUS "Looking for CUDD library installed by build.sh...")

find_path(CUDD_INCLUDE_DIR cudd.h
          HINTS /usr/local /usr
          PATH_SUFFIXES include include/cudd)
find_library(CUDD_LIBRARY NAMES cudd.a cudd
          HINTS /usr/local /usr
          PATH_SUFFIXES lib lib64)

if(CUDD_INCLUDE_DIR AND CUDD_LIBRARY)
    # 创建一个 IMPORTED 静态库目标，指向 build.sh 安装的 CUDD 库
    # 这里使用 STATIC 是因为 cudd.a 是静态库。如果它也可以是动态库，可以根据实际情况调整。
    add_library(CUDD::cudd STATIC IMPORTED)
    set_target_properties(CUDD::cudd PROPERTIES
                            IMPORTED_LOCATION "${CUDD_LIBRARY}"
                            INTERFACE_INCLUDE_DIRECTORIES "${CUDD_INCLUDE_DIR}")
    message(STATUS "Found CUDD installed by build.sh: ${CUDD_LIBRARY} (include: ${CUDD_INCLUDE_DIR})")
else()
    message(FATAL_ERROR "CUDD library or headers not found after build.sh installation. Please ensure CUDD is correctly installed by build.sh into /usr/local.")
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