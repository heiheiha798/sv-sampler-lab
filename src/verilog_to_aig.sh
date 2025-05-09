# 脚本名称: verilog_to_aig.sh
# 用途: 将输入的 Verilog 文件使用 Yosys 综合成 AIG 文件。
# 用法: ./verilog_to_aig.sh /path/to/your/input.v

# 检查是否提供了输入文件参数
if [ -z "$1" ]; then
    echo "错误: 请提供一个 Verilog 文件的路径作为参数。"
    echo "用法: $0 <input_verilog_file>"
    exit 1
fi

INPUT_VERILOG_FILE="$1"

# 检查输入文件是否存在且是 .v 文件
if [ ! -f "$INPUT_VERILOG_FILE" ]; then
    echo "错误: 输入文件 '$INPUT_VERILOG_FILE' 不存在。"
    exit 1
fi

if [[ "${INPUT_VERILOG_FILE##*.}" != "v" ]]; then
    echo "错误: 输入文件 '$INPUT_VERILOG_FILE' 不是一个 .v 文件。"
    exit 1
fi

# 获取输入文件的目录和不带扩展名的文件名
INPUT_DIR=$(dirname "$INPUT_VERILOG_FILE")
BASENAME=$(basename "$INPUT_VERILOG_FILE" .v)
OUTPUT_AIG_FILE="${INPUT_DIR}/${BASENAME}.aig"

# Yosys 命令
YOSYS_SCRIPT_CONTENT="
read_verilog \"${INPUT_VERILOG_FILE}\";
synth -auto-top;
aigmap;
# techmap;
opt;     # 可选优化
clean;   # 清理未使用的逻辑
write_aiger \"${OUTPUT_AIG_FILE}\";
"

echo "--- Yosys 命令将执行: ---"
echo "$YOSYS_SCRIPT_CONTENT"
echo "--------------------------"
echo "输入 Verilog 文件: $INPUT_VERILOG_FILE"
echo "输出 AIG 文件: $OUTPUT_AIG_FILE"

# 使用 -q 选项来减少 Yosys 的输出（静默模式），如果想看详细输出可以去掉 -q
yosys -q -p "$YOSYS_SCRIPT_CONTENT"

# 检查 Yosys 是否成功执行
if [ $? -eq 0 ]; then
    echo "成功: '$INPUT_VERILOG_FILE' 已综合为 '$OUTPUT_AIG_FILE'"
else
    echo "错误: Yosys 执行失败。"
    exit 1
fi

exit 0