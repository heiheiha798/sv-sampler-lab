# #!/bin/bash
# set -e

# sudo apt update
# sudo apt install -y \
#     git \
#     build-essential \
#     libstdc++-12-dev \
#     libc++-dev \
#     pkg-config \
#     tcl tcl-dev \
#     clang \
#     flex bison \
#     libreadline-dev \
#     gawk\
#     automake\
#     autoconf\
#     libtool
# git submodule update --init --recursive

# echo "Compiling Yosys..."
# cd /root/sv-sampler-lab/yosys/
# make -j$(nproc)

# cd /root/sv-sampler-lab/cudd
# autoreconf -fvi
# ./configure
# make -j$(nproc)
# sudo make install

# cd /root/sv-sampler-lab/

# rm -rf build/
# mkdir -p ./build
# cd ./build
# cmake ..
# make -j$(nproc)

# echo "Build process completed successfully! Please try run.sh"


#!/bin/bash

# 确保脚本在遇到错误时立即退出
set -e

# 定义日志文件路径
LOG_FILE="build_output.log"

# 清空或创建日志文件
> "$LOG_FILE"

# 将所有后续命令的标准输出和标准错误重定向到日志文件
exec 1>> "$LOG_FILE" 2>&1

# --- 开始构建过程 (这些 echo 语句需要明确重定向到 /dev/tty) ---
echo "--- 开始构建过程 ---" > /dev/tty

# --- 1. 安装系统依赖 ---
echo "正在安装系统依赖 (可能需要 sudo 密码)..." > /dev/tty
sudo apt update
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
    gawk \
    automake \
    autoconf \
    libtool
echo "系统依赖安装完成。" > /dev/tty


# --- 2. 初始化并更新 Git 子模块 ---
echo "正在初始化并更新 Git 子模块..." > /dev/tty
git submodule update --init --recursive
echo "Git 子模块更新完成。" > /dev/tty


# --- 3. 编译第三方库 (子模块) ---

# 3.1 编译 Yosys
echo "正在编译 Yosys (可能需要一些时间)..." > /dev/tty
cd ./yosys/
make -j$(nproc) # Yosys 的构建系统通常只需要直接执行 make
cd ../
echo "Yosys 编译完成。" > /dev/tty


# 3.2 编译 CUDD
echo "正在编译 CUDD (可能需要一些时间)..." > /dev/tty
cd ./cudd/
autoreconf -fvi # CUDD 使用 Autotools，需要这一步
./configure
make -j$(nproc)
sudo make install
cd ../
echo "CUDD 编译和安装完成。" > /dev/tty


# --- 4. 编译您自己的主项目 ---
echo "正在编译主项目 (MySolver)..." > /dev/tty
rm -rf build/
mkdir -p ./build
cd ./build
cmake ..
make -j$(nproc)
echo "主项目编译完成。" > /dev/tty


# --- 构建完成消息 ---
# 确保这些最终消息也显示在终端
echo -e "\n--- 构建过程成功完成！---" > /dev/tty
echo "详细输出请查看 '$LOG_FILE' 文件。" > /dev/tty
echo "现在可以尝试运行求解器：./run.sh" > /dev/tty

# 脚本结束