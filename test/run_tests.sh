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

test_num=0
failed_test=""
rc=0
test_bins="test_butil test_bvar bthread*unittest brpc*unittest"
ulimit -c unlimited # turn on coredumps
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
    # find newest core file
    COREFILE=$(find . -name "core*" -type f -printf "%T@ %p\n" | sort -k 1 -n | cut -d' ' -f 2- | tail -n 1)
    if [ ! -z "$COREFILE" ]; then
        >&2 echo "corefile=$COREFILE prog=$1"
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
