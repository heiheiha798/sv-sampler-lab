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

echo "--- 开始运行求解器程序 ---"

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


# 1. 运行 MySolver 执行 JSON 到 Verilog 转换
# 调用 MySolver，传递 "json-to-v" 标志，输入 JSON 文件路径，输出 Verilog 目录
echo "正在运行 MySolver (JSON to Verilog 模式)..."
./build/MySolver json-to-v "$CONSTRAINT_JSON" "$RUN_DIR"

# 检查 MySolver (JSON to Verilog) 是否成功执行
if [ $? -eq 0 ]; then
    echo "成功: Verilog 文件生成完成: '$GENERATED_V_FILE'"
else
    echo "错误: MySolver (JSON to Verilog) 执行失败。"
    exit 1
fi


# 2. 使用 Yosys 处理生成的 Verilog 文件并生成 AIG
echo "正在运行 Yosys 将 Verilog 转换为 AIG..."

# 假设 Yosys 的可执行文件路径 (从 build.sh 确认过)
YOSYS_EXECUTABLE=/root/sv-sampler-lab/yosys/yosys

# 检查生成的 .v 文件是否存在 (保险起见，虽然上一步已检查)
if [ ! -f "$GENERATED_V_FILE" ]; then
    echo "错误: Verilog 文件 '$GENERATED_V_FILE' 不存在，Yosys 无法执行。"
    exit 1
fi

# Yosys 脚本内容
# 修正：在 write_aiger 命令中添加 -ascii 选项
YOSYS_SCRIPT_CONTENT="
read_verilog \"${GENERATED_V_FILE}\";
synth -auto-top;
aigmap;
# techmap; # techmap 可能用于更具体的映射，如果不需要可以注释
opt;
clean;
write_aiger -ascii \"${OUTPUT_AIG_FILE}\";
"

echo "--- Yosys 命令将执行: ---"
echo "$YOSYS_SCRIPT_CONTENT"
echo "--------------------------"
echo "输入 Verilog 文件: $GENERATED_V_FILE"
echo "输出 AIG 文件: $OUTPUT_AIG_FILE"

# 使用 -q 选项来减少 Yosys 的输出（静默模式）
"$YOSYS_EXECUTABLE" -q -p "$YOSYS_SCRIPT_CONTENT"

# 检查 Yosys 是否成功执行
if [ $? -eq 0 ]; then
    echo "成功: '$GENERATED_V_FILE' 已综合为 '$OUTPUT_AIG_FILE'"
else
    echo "错误: Yosys 执行失败。"
    exit 1
fi

# 3. 运行 MySolver 执行 AIG 到 BDD 求解
# 调用 MySolver，传递 "aig-to-bdd" 标志，AIG 文件路径，原始 JSON 路径，样本数，结果 JSON 路径，随机种子
echo -e "\n正在运行 MySolver (AIG to BDD 求解模式)..."

# 检查生成的 .aig 文件是否存在
if [ ! -f "$OUTPUT_AIG_FILE" ]; then
    echo "错误: AIG 文件 '$OUTPUT_AIG_FILE' 不存在，BDD 求解器无法执行。"
    exit 1
fi

./build/MySolver aig-to-bdd "$OUTPUT_AIG_FILE" "$CONSTRAINT_JSON" "$NUM_SAMPLES" "$FINAL_RESULT_JSON" "$RANDOM_SEED"

# 检查 MySolver (BDD 求解) 是否成功执行
if [ $? -eq 0 ]; then
    echo "成功: BDD 求解完成。结果已写入到 '$FINAL_RESULT_JSON'"
else
    echo "错误: MySolver (AIG to BDD) 执行失败。"
    exit 1
fi

echo -e "\n--- 求解器程序运行完成！---"