#!/bin/bash
set -e

# 获取当前脚本的目录，并将其赋值给 SCRIPT_DIR 变量
SCRIPT_DIR=$(dirname "$0")

apt install sudo
sudo apt update
sudo apt-get update
sudo apt-get -y install python3 bc python-is-python3
sudo apt install -y \
    git \
    build-essential \
    libstdc++-12-dev \
    libc++-dev \
    pkg-config \
    tcl tcl-dev \
    clang \
    flex bison \
    libreadline-dev \
    libgcc-11-dev \
    gfortran \
    gawk \
    automake \
    autoconf \
    libtool \
    gcc \
    g++
git submodule update --init --recursive

echo "Compiling Yosys..."
# 使用 SCRIPT_DIR 变量来构建相对路径
cd "$SCRIPT_DIR"/yosys/
make -j$(nproc)

# 使用 SCRIPT_DIR 变量来构建相对路径
cd "$SCRIPT_DIR"/cudd
autoreconf -fvi
./configure
make -j$(nproc)
sudo make install

# 使用 SCRIPT_DIR 变量来构建相对路径
cd "$SCRIPT_DIR"/

rm -rf build/
mkdir -p ./build
cd ./build
cmake ..
make -j$(nproc)

echo "Build process completed successfully! Please try run.sh"
