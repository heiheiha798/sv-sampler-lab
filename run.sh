#!/bin/bash

set -e # Exit immediately if a command exits with a non-zero status.

# Default Yosys path - can be overridden by environment variable or argument
DEFAULT_YOSYS_PATH="./yosys/yosys" # Assuming yosys is in a subdirectory
MY_SOLVER_EXEC="./build/MySolver"  # Assuming your solver is built in ./build

# --- Argument Parsing ---
if [ "$#" -lt 3 ] || [ "$#" -gt 5 ]; then
    echo "Usage: $0 <constraint_json_path> <num_samples> <run_output_dir> [random_seed] [yosys_executable_path]"
    echo "  constraint_json_path: Path to the input JSON file."
    echo "  num_samples: Number of satisfying assignments to generate."
    echo "  run_output_dir: Directory to store all intermediate and final files."
    echo "  random_seed (optional): Seed for randomization. Defaults to 12345."
    echo "  yosys_executable_path (optional): Path to Yosys executable. Defaults to '$DEFAULT_YOSYS_PATH'."
    exit 1
fi

CONSTRAINT_JSON=$1
NUM_SAMPLES=$2
RUN_DIR=$3
RANDOM_SEED=${4:-12345} # Default seed if not provided
YOSYS_EXECUTABLE=${5:-$DEFAULT_YOSYS_PATH}

# --- Validate Inputs ---
if [ ! -f "$CONSTRAINT_JSON" ]; then
    echo "Error: Constraint JSON file not found: $CONSTRAINT_JSON"
    exit 1
fi

if ! [[ "$NUM_SAMPLES" =~ ^[0-9]+$ ]] || [ "$NUM_SAMPLES" -lt 0 ]; then
    echo "Error: Number of samples must be a non-negative integer."
    exit 1
fi

if ! [[ "$RANDOM_SEED" =~ ^[0-9]+$ ]]; then
    echo "Error: Random seed must be an integer."
    exit 1
fi

if [ -n "$5" ] && [ ! -x "$YOSYS_EXECUTABLE" ]; then # Check Yosys path only if explicitly provided
    echo "Error: Yosys executable not found or not executable: $YOSYS_EXECUTABLE (Note: if using default, ensure it exists or provide path)"
    # exit 1 # Soft error for now, MySolver might use a system-wide yosys
fi


# --- Setup Environment ---
mkdir -p "$RUN_DIR"
# Convert RUN_DIR to absolute path for robustness if MySolver changes CWD
RUN_DIR_ABS=$(realpath "$RUN_DIR")
CONSTRAINT_JSON_ABS=$(realpath "$CONSTRAINT_JSON")


# --- Main Execution ---
echo "Starting decomposed solver pipeline..."
echo "Input JSON: $CONSTRAINT_JSON_ABS"
echo "Number of Samples: $NUM_SAMPLES"
echo "Run Directory: $RUN_DIR_ABS"
echo "Random Seed: $RANDOM_SEED"
echo "Yosys Executable: $YOSYS_EXECUTABLE"

# Call the main orchestrator mode of MySolver
"$MY_SOLVER_EXEC" solve-decomposed "$CONSTRAINT_JSON_ABS" "$NUM_SAMPLES" "$RUN_DIR_ABS" "$RANDOM_SEED" "$YOSYS_EXECUTABLE"

# Check the exit code of MySolver
SOLVER_EXIT_CODE=$?
if [ $SOLVER_EXIT_CODE -eq 0 ]; then
    echo "Pipeline completed successfully."
    echo "Final results should be in $RUN_DIR_ABS/result.json"
elif [ $SOLVER_EXIT_CODE -eq 2 ]; then # Special code for UNSAT by json_v_converter
    echo "Pipeline determined the problem is UNSAT during preprocessing."
    echo "Results indicating UNSAT should be in $RUN_DIR_ABS/result.json"
else
    echo "Error: MySolver pipeline failed with exit code $SOLVER_EXIT_CODE."
    exit $SOLVER_EXIT_CODE
fi

echo "Script finished."