#!/bin/bash

# 确保脚本在遇到错误时立即退出
set -e

# 从命令行参数获取输入 (根据 README.md 的 run.sh 调用方式)
CONSTRAINT_JSON=$1    # 第一个参数：约束 JSON 文件路径
NUM_SAMPLES=$2        # 第二个参数：生成解的数量
RUN_DIR=$3            # 第三个参数：运行结果和中间文件的输出目录
RANDOM_SEED=$4        # 第四个参数：随机种子

# 确保 RUN_DIR 目录存在
mkdir -p "$RUN_DIR"

echo "--- 开始运行求解器程序 ---"

# 1. 运行您的 JSON 到 Verilog 转换程序 (MySolver)
echo "正在运行 JSON 到 Verilog 转换器..."
# 您的可执行文件 MySolver 位于 build 目录下
./build/MySolver "$CONSTRAINT_JSON" "$NUM_SAMPLES" "$RUN_DIR" "$RANDOM_SEED"
echo "Verilog 文件已生成在 $RUN_DIR。"


# 2. 使用 Yosys 处理生成的 Verilog 文件并生成 AIG
echo "正在运行 Yosys 将 Verilog 转换为 AIG..."

# 假设 Yosys 的可执行文件路径 (从 build.sh 确认过)
YOSYS_EXECUTABLE=/root/sv-sampler-lab/yosys/yosys

# 动态构建 MySolver 程序生成的 .v 文件路径和 .aig 文件路径
# 根据 json_v_converter 的逻辑，文件名将是 [parent_dir_name]_[filename_no_ext].v
INPUT_JSON_BASENAME=$(basename "$CONSTRAINT_JSON" .json) # 从 .json 文件名提取不带扩展名的部分
PARENT_DIR=$(basename "$(dirname "$CONSTRAINT_JSON")") # 提取上级目录名

# 构建生成的 .v 文件路径
# 如果 JSON 文件位于项目根目录的子文件夹内（例如 basic/0.json, opt1/0.json）
if [ -n "$PARENT_DIR" ] && [ "$PARENT_DIR" != "." ] && [ "$PARENT_DIR" != "/" ]; then # 增加对 / 的检查
    GENERATED_V_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.aig"
else
    # 如果 JSON 文件直接在项目根目录（例如 0.json）
    GENERATED_V_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.aig"
fi

# 检查生成的 .v 文件是否存在
if [ ! -f "$GENERATED_V_FILE" ]; then
    echo "错误: 转换器未生成 Verilog 文件 '$GENERATED_V_FILE'。"
    exit 1
fi

# Yosys 脚本内容
YOSYS_SCRIPT_CONTENT="
read_verilog \"${GENERATED_V_FILE}\";
synth -auto-top;
aigmap;
# techmap; # techmap 可能用于更具体的映射，如果不需要可以注释
opt;
clean;
write_aiger \"${OUTPUT_AIG_FILE}\";
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

# 3. (待续) 使用 CUDD 库处理 AIG 文件并生成最终结果 (result.json)
# 这将是您核心 BDD 求解算法的实现部分

echo -e "\n--- 求解器程序运行至 Yosys 阶段完成！---"
echo "现在可以继续编写 BDD 求解部分。"

# 脚本结束