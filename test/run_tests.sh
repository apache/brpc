#/bin/bash -f
test_num=0
failed_test=""
rc=0
test_bins="test_butil test_bvar bthread*unittest brpc*unittest"
for test_bin in $test_bins; do
    test_num=$((test_num + 1))
    >&2 echo "[runtest] $test_bin"
    ./$test_bin
    rc=$?
    if [ $rc -ne 0 ]; then
        failed_test="$test_bin"
        break;
    fi
done
if [ $test_num -eq 0 ]; then
    >&2 echo "[runtest] Cannot find any tests"
    exit 1
fi
print_bt () {
    COREFILE=$(find . -maxdepth 2 -name "core*" | head -n 1) # find core file
    if [[ -f "$COREFILE" ]]; then
        gdb -c "$COREFILE" $1 -ex "thread apply all bt" -ex "set pagination 0" -batch;
    fi
}
if [ -z "$failed_test" ]; then
    >&2 echo "[runtest] $test_num succeeded"
elif [ $test_num -gt 1 ]; then
    print_bt $failed_test
    >&2 echo "[runtest] '$failed_test' failed, $((test_num-1)) succeeded"
else
    print_bt $failed_test
    >&2 echo "[runtest] '$failed_test' failed"
fi
exit $rc
