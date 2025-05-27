#!/bin/bash
set -e

PROJECT_ROOT=$(pwd) # 获取项目根目录的绝对路径
INSTALL_PREFIX_ABS="${PROJECT_ROOT}/_install" # 构建绝对安装路径
mkdir -p "$INSTALL_PREFIX_ABS"

# 假设当前工作目录已经是 sv-sampler-lab 的根目录
git submodule update --init --recursive

echo "Compiling Yosys..."
# 相对路径 cd
cd ./yosys/
make -j$(nproc)

# 返回上一级目录 (sv-sampler-lab 根目录)
cd ../

# 相对路径 cd
cd ./cudd
autoreconf -fvi
./configure --prefix="$INSTALL_PREFIX_ABS"
make -j$(nproc)
make install

# 返回上一级目录 (sv-sampler-lab 根目录)
cd ../

rm -rf build/
mkdir -p ./build
cd ./build
# cmake -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX_ABS" ..
cmake -DCMAKE_PREFIX_PATH="$INSTALL_PREFIX_ABS" ..
make -j$(nproc)

echo "Build process completed successfully! Please try run.sh"
