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

while read -r line; do
    val=$(echo "$line" | awk '{print $3}')
    if   [[ $line =~ __clang_major__ ]]; then
        CLANG_MAJOR=${val}
    elif [[ $line =~ __clang_minor__ ]]; then
        CLANG_MINOR=${val}
    elif [[ $line =~ __clang__ ]]; then
        CLANG=${val}
    elif [[ $line =~ __GNUC__ ]]; then
        GNUC=${val}
    elif [[ $line =~ __GNUC_MINOR__ ]]; then
        GNUC_MINOR=${val}
    elif [[ $line =~ __GNUC_PATCHLEVEL__ ]]; then
        GNUC_PATCHLEVEL=${val}
    fi
done < <("${CXX:-c++}" -dM -E - < /dev/null | grep "__clang__\|__clang_major__\|__clang_minor__\|__GNUC__\|__GNUC_MINOR__\|__GNUC_PATCHLEVEL__")

if [ -n "$CLANG" ] && [ -n "$CLANG_MAJOR" ] && [ -n "$CLANG_MINOR" ]; then
    if [ "$CLANG_MAJOR" -gt 3 ] || \
       { [ "$CLANG_MAJOR" -eq 3 ] && [ "$CLANG_MINOR" -ge 5 ]; }; then
        # config_brpc.sh uses the GCC-equivalent version threshold.
        echo 50000
    else
        echo 0
    fi
elif [ -n "$GNUC" ] && [ -n "$GNUC_MINOR" ] && [ -n "$GNUC_PATCHLEVEL" ]; then
    # Calculate GCC version.
    GCC_VERSION=$((GNUC * 10000 + GNUC_MINOR * 100 + GNUC_PATCHLEVEL))
    echo $GCC_VERSION
else
    echo 0
fi
