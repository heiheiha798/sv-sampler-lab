#!/usr/bin/bash

run() {
    local tlim=$1
    local cnstr_file=$2
    local run_dir=$3
    local seed=$4

    local start_time=$(date +%s%N)
    timeout $tlim ./run.sh ${cnstr_file} 1000 ${run_dir} ${seed} 2>&1 > /dev/null
    local retcode=$?
    local end_time=$(date +%s%N)

    if [ $retcode -ne 0 ]; then
        if [ $retcode -eq 124 ]; then
            echo "Timeout." >&2 
            echo "$tlim"
        else
            echo "Runtime Error." >&2 
            echo "-1"
        fi
        return 1
    fi

    # local valid=$(./evalcns -p ${cnstr_file} -a ${run_dir}/result.json 2> /dev/null | awk -f parse_evalcns.awk)
    local evalcns_stdout_file="${run_dir}/evalcns_stdout.txt"
    local evalcns_stderr_file="${run_dir}/evalcns_stderr.txt"

    # 执行 evalcns 并将输出重定向到文件
    ./evalcns -p ${cnstr_file} -a ${run_dir}/result.json > "${evalcns_stdout_file}" 2> "${evalcns_stderr_file}"
    local evalcns_retcode=$? # 获取 evalcns 的退出码

    local valid_awk_output=""
    # 确保 evalcns 的标准输出文件存在且不为空再用 awk 处理
    if [ -s "${evalcns_stdout_file}" ]; then
        valid_awk_output=$(awk -f parse_evalcns.awk "${evalcns_stdout_file}")
    fi
    
    # 首先检查 evalcns 本身是否运行出错
    if [ $evalcns_retcode -ne 0 ]; then
        echo "Solution checking command './evalcns' had runtime error (exit code $evalcns_retcode) in ${run_dir}" >&2
        echo "Stderr from evalcns is in ${evalcns_stderr_file}" >&2
        # 可以选择性地打印部分 stderr 内容，或者提示用户查看文件
        # head -n 20 "${evalcns_stderr_file}" >&2
        echo "-1"
        return 1
    fi
    
    # 然后检查 awk 处理后的结果是否为 "100..."
    # (与您脚本中原有的 'valid' 变量名保持一致)
    local valid="${valid_awk_output}"

    if [ "${valid:0:3}" != "100" ]; then
        echo "Solution checking does not pass in ${run_dir} (evalcns/awk output: '${valid}')" >&2
        echo "  To debug, check files in ${run_dir}:" >&2
        echo "  Constraint file: ${cnstr_file}" >&2
        echo "  Solver result: ${run_dir}/result.json" >&2
        echo "  Evalcns stdout: ${evalcns_stdout_file}" >&2
        echo "  Evalcns stderr: ${evalcns_stderr_file}" >&2
        # 如果需要，可以在此处打印文件的部分内容，例如：
        # echo "--- Evalcns Stdout Preview ---" >&2
        # head -n 20 "${evalcns_stdout_file}" >&2
        # echo "--- Evalcns Stderr Preview ---" >&2
        # head -n 20 "${evalcns_stderr_file}" >&2
        echo "-1"
        return 1
    fi

    # 如果通过，则继续计算运行时间等...
    local elapsed_ns=$((end_time-start_time))
    if [ $? -ne 0 ]; then
        echo "Solution checking has runtime error in ${run_dir}" >&2
        echo "-1"
        return 1
    fi
    
    if [ "${valid:0:3}" != "100" ]; then
        echo "Solution checking does not pass in ${run_dir}" >&2
        echo "-1"
        return 1
    fi

    local elapsed_ns=$((end_time-start_time))
    local elapsed_s=$(echo "scale=3; $elapsed_ns / 1000000000" | bc)
    echo "$elapsed_s"

    return 0
}

evaluate() {
    local tlim=$1
    local cnstr_file=$2
    local run_dir=$3

    local runtime=(0 0 0 0 0 0 0 0 0 0)
    for((i=0;i<10;i=i+1)); do
        mkdir -p ${run_dir}/$i
        run $tlim $cnstr_file ${run_dir}/$i $i > ${run_dir}/${i}/runtime.txt &
    done

    wait

    for((i=0;i<10;i=i+1)); do
        runtime[$i]=$(< "${run_dir}/${i}/runtime.txt")
    done

    for((i=0;i<10;i=i+1)); do
        if [ "${runtime[$i]}" == "-1" ]; then
            echo "Failed\n" >&2
            return 1
        fi
    done

    echo "Runtimes runninng ${cnstr_file}" >&2
    echo "${runtime[@]}" >&2
    local result=($(echo "${runtime[@]}" | python -c "times=list(map(float, input().split())); \
                                               times=sorted(times); \
                                               print(times[-1], times[-3], (times[4]+times[5])/2)"))
    
    echo "${result[@]}"
    echo "Max: ${result[0]}, 8-th: ${result[1]}, Median: ${result[2]}" >&2
}

test() {
    local dir=$1
    local tlim=$2
    local max_time=$3
    local total_score=$4
    local result_id=${5:-0}
    local id=$6
    
    echo "Testing ${dir}"
    local rundir="_run/${dir}"

    if [ "$id" != "" ]; then
        mkdir -p "$rundir/$id"
        evaluate $tlim $dir/$id.json "$rundir/$id"
    else
        local score=70
        local total=$(find ${dir} -name "*.json" | wc -l)
        local passed=0

        rm -rf $rundir

        for((i=0;i<$total;i=i+1)); do
            local cnstr_file=$dir/$i.json
            mkdir -p "$rundir/$i"
            local result=($(evaluate $tlim $cnstr_file "$rundir/$i"))
            local verdict=$(echo "scale=3; ${result[$result_id]} <= ${max_time}" | bc)
            
            if [ "$verdict" == "1" ]; then
                local passed=$((passed+1))
            fi
        done

        echo -n "Score ${dir}: "
        echo $(echo "scale=3; $total_score * $passed / $total" | bc)
    fi
}

test_basic() {
    test basic 70 60 70 $1
}

test_opt1() {
    test opt1 50 30 4 $1
}

test_opt2() {
    test opt2 150 120 4 $1
}

test_opt3() {
    test opt3 30 15 6 $1
}

test_opt4() {
    test opt4 150 120 4 $1
}

test_opt5() {
    test opt5 50 20 12 $1 1
}

test_all() {
    test_basic
    test_opt1
    test_opt2
    test_opt3
    test_opt4
    test_opt5
}

part=$1
id=$2

if [ "$part" == "basic" ]; then
    test_basic $id
elif [ "$part" == "opt1" ]; then
    test_opt1 $id
elif [ "$part" == "opt2" ]; then
    test_opt2 $id
elif [ "$part" == "opt3" ]; then
    test_opt3 $id
elif [ "$part" == "opt4" ]; then
    test_opt4 $id
elif [ "$part" == "opt5" ]; then
    test_opt5 $id
elif [ "$part" == "all" ]; then
    test_all
else
    echo "Usage: ./evaluate [basic|opt1|opt2|opt3|opt4|opt5|all] {id}"
    return 1
fi