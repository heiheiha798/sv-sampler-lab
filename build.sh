#!/bin/bash
set -e

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
./configure
make -j$(nproc)
sudo make install

# 返回上一级目录 (sv-sampler-lab 根目录)
cd ../

rm -rf build/
mkdir -p ./build
cd ./build
cmake ..
make -j$(nproc)

echo "Build process completed successfully! Please try run.sh"
