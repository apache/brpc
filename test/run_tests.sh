#!/usr/bin/env bash

# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# turn on coredumps
ulimit -c unlimited
rm -f core.*

test_num=0
failed_test=""
rc=0
test_bins="test_butil test_bvar bthread*unittest brpc*unittest"
export ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1"
# Keep the original stdin on fd 3 for asynchronous test processes.
for test_bin in $test_bins; do
    test_num=$((test_num + 1))
    >&2 echo "[runtest] $test_bin"
    # A passing death test may leave a core behind. Keep only cores from the
    # binary about to run so a later failure is symbolized against its core.
    rm -f core.*
    # Preserve stdin while starting the test asynchronously to capture its PID.
    ./"$test_bin" <&3 3<&- &
    test_pid=$!
    wait "$test_pid"
    # If ASan abort without detailed call stack of new/delete,
    # try to disable fast_unwind_on_malloc, which would be a performance killer.
    # ASAN_OPTIONS="fast_unwind_on_malloc=0:detect_leaks=0" ./$test_bin
    rc=$?
    if [ $rc -ne 0 ]; then
        failed_test="$test_bin"
        break;
    fi
done 3<&0
if [ $test_num -eq 0 ]; then
    >&2 echo "[runtest] Cannot find any tests"
    exit 1
fi

print_bt () {
    case "${2-}" in
        ''|*[!0-9]*) return ;;
    esac
    if [ "$2" -le 0 ]; then
        return
    fi
    # CI uses core.%e.%p; also accept core.%p. Only symbolize a core from the
    # failed test process; a passing death test has a different PID.
    # Match the current-directory cleanup above; ignore nested stale cores.
    COREFILE=
    for candidate in ./core.*."$2" ./core."$2"; do
        if [ ! -f "$candidate" ] || [ -L "$candidate" ]; then
            continue
        fi
        if [ -z "$COREFILE" ] || [ "$candidate" -nt "$COREFILE" ]; then
            COREFILE=$candidate
        fi
    done
    if [ ! -z "$COREFILE" ]; then
        >&2 echo "corefile=$COREFILE prog=$1"
        gdb -c "$COREFILE" "$1" -ex "bt" -ex "thread apply all bt" -ex "set pagination 0" -batch;
    fi
}

if [ -z "$failed_test" ]; then
    >&2 echo "[runtest] $test_num succeeded"
elif [ $test_num -gt 1 ]; then
    print_bt "$failed_test" "$test_pid"
    >&2 echo "[runtest] '$failed_test' failed, $((test_num-1)) succeeded"
else
    print_bt "$failed_test" "$test_pid"
    >&2 echo "[runtest] '$failed_test' failed"
fi
exit $rc
