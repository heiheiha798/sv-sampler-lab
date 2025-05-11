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
git submodule update --init --recursive

echo "Compiling Yosys..."
cd /root/sv-sampler-lab/yosys/
make -j$(nproc)

cd /root/sv-sampler-lab/cudd
autoreconf -fvi
./configure
make -j$(nproc)
sudo make install

cd /root/sv-sampler-lab/

rm -rf build/
mkdir -p ./build
cd ./build
cmake ..
make -j$(nproc)

echo "Build process completed successfully! Please try run.sh"
