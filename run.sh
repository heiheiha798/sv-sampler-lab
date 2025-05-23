#!/bin/bash

set -e

CONSTRAINT_JSON=$1
NUM_SAMPLES=$2
RUN_DIR=$3 
RANDOM_SEED=$4 

# 获取当前脚本的目录
SCRIPT_DIR=$(dirname "$0")

mkdir -p "$RUN_DIR"

INPUT_JSON_BASENAME=$(basename "$CONSTRAINT_JSON" .json) 
PARENT_DIR=$(basename "$(dirname "$CONSTRAINT_JSON")")

if [ -n "$PARENT_DIR" ] && [ "$PARENT_DIR" != "." ] && [ "$PARENT_DIR" != "/" ]; then 
    GENERATED_V_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${PARENT_DIR}_${INPUT_JSON_BASENAME}.aig"
else
    GENERATED_V_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.v"
    OUTPUT_AIG_FILE="$RUN_DIR/${INPUT_JSON_BASENAME}.aig"
fi

FINAL_RESULT_JSON="$RUN_DIR/result.json"
"$SCRIPT_DIR"/build/MySolver json-to-v "$CONSTRAINT_JSON" "$RUN_DIR"
YOSYS_EXECUTABLE="$SCRIPT_DIR"/yosys/yosys
YOSYS_SCRIPT_CONTENT="
read_verilog \"${GENERATED_V_FILE}\";
synth -auto-top;
abc;
aigmap;
opt;
clean;
write_aiger -ascii \"${OUTPUT_AIG_FILE}\";
"

"$YOSYS_EXECUTABLE" -q -p "$YOSYS_SCRIPT_CONTENT"
"$SCRIPT_DIR"/build/MySolver aig-to-bdd "$OUTPUT_AIG_FILE" "$CONSTRAINT_JSON" "$NUM_SAMPLES" "$FINAL_RESULT_JSON" "$RANDOM_SEED"
