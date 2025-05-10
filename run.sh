#!/bin/bash

# 确保脚本在遇到错误时立即退出
set -e

# 从命令行参数获取输入 (根据 README.md 的 run.sh 调用方式)
CONSTRAINT_JSON=$1 # 第一个参数：约束 JSON 文件路径
NUM_SAMPLES=$2 # 第二个参数：生成解的数量
RUN_DIR=$3 # 第三个参数：运行结果和中间文件的输出目录
RANDOM_SEED=$4 # 第四个参数：随机种子

# 确保 RUN_DIR 目录存在
mkdir -p "$RUN_DIR"

# 动态构建生成文件的路径
INPUT_JSON_BASENAME=$(basename "$CONSTRAINT_JSON" .json) # 从 .json 文件名提取不带扩展名的部分
PARENT_DIR=$(basename "$(dirname "$CONSTRAINT_JSON")") # 提取上级目录名

# 构建生成的 .v 文件路径和 .aig 文件路径
# 如果 JSON 文件位于项目根目录的子文件夹内（例如 basic/0.json, opt1/0.json）
if [ -n "$PARENT_DIR" ] && [ "$PARENT_DIR" != "." ] && [ "$PARENT_DIR" != "/" ]; then # 增加对 / 的检查
    GENERATED_V_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.aig"
else
    # 如果 JSON 文件直接在项目根目录（例如 0.json）
    GENERATED_V_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.aig"
fi

# 定义最终结果 JSON 文件的路径
FINAL_RESULT_JSON="$RUN_DIR/result.json"

./build/MySolver json-to-v "$CONSTRAINT_JSON" "$RUN_DIR"

# 假设 Yosys 的可执行文件路径 (从 build.sh 确认过)
YOSYS_EXECUTABLE=/root/sv-sampler-lab/yosys/yosys

# Yosys 脚本内容
# --- 版本1: 简单地在现有流程中加入ABC的默认优化 ---
YOSYS_SCRIPT_CONTENT="
read_verilog \"${GENERATED_V_FILE}\";
synth -auto-top;
abc; # <--- 加入这行，使用ABC的默认优化
aigmap;
opt;
clean;
write_aiger -ascii \"${OUTPUT_AIG_FILE}\";
"


# 使用 -q 选项来减少 Yosys 的输出（静默模式）
"$YOSYS_EXECUTABLE" -q -p "$YOSYS_SCRIPT_CONTENT"

./build/MySolver aig-to-bdd "$OUTPUT_AIG_FILE" "$CONSTRAINT_JSON" "$NUM_SAMPLES" "$FINAL_RESULT_JSON" "$RANDOM_SEED"
