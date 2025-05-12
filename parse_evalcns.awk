/PASS/ {
    flag=1;

    split($2, count_field, ":")
    split($4, score_field, ":")

    if (length(count_field) >= 2 && length(score_field) >= 2) {
        printf "%s %s\n", count_field[2], score_field[2]
    } else {
        print "AWK Error: PASS 行格式与预期不符（字段 $2 或 $4 无法按 ':' 分割）" > "/dev/stderr"
        printf "0 0.0\n"
    }
}

END {
    if (flag == 0) {
        printf "0 0.0\n"
    }
}
