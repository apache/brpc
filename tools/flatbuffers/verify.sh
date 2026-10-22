#!/usr/bin/env bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements. See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership. The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License. You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied. See the License for the
# specific language governing permissions and limitations
# under the License.

set -euo pipefail
if [[ $# != 1 ]]; then
    printf 'Usage: bash tools/flatbuffers/verify.sh <build-directory>\n' >&2
    exit 2
fi
root=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$1"
work=$(cd "$1" && pwd)
cmake=${CMAKE:-cmake}
ctest=${CTEST:-ctest}
cxx=${CXX:-c++}
jobs=${JOBS:-2}
runtime=${CODEGEN_RUNTIME:-ON}
case "$runtime" in
    ON|OFF) ;;
    *) printf 'CODEGEN_RUNTIME must be ON or OFF\n' >&2; exit 2 ;;
esac

printf 'Source: %s\nWork: %s\nCODEGEN_RUNTIME: %s\n' "$root" "$work" "$runtime"
uname -sm
"$cxx" --version
"$cmake" --version
"$ctest" --version
"$cmake" -E sha256sum "$root/tools/flatbuffers/brpc_flatc.cpp" \
    "$root/test/flatbuffers_codegen/acceptance.cmake" \
    "$root/test/flatbuffers_codegen/CMakeLists.txt" \
    "$root/test/flatbuffers_codegen/echo.fbs" \
    "$root/test/flatbuffers_codegen/runtime.cpp"

# Keep the archive and checksum in sync with MODULE.bazel and WORKSPACE.
if [[ -z ${FLATBUFFERS_PREFIX:-} ]]; then
    archive="$work/flatbuffers-25.2.10.tar.gz"
    curl --fail --location --retry 3 \
        https://github.com/google/flatbuffers/archive/refs/tags/v25.2.10.tar.gz \
        --output "$archive"
    checksum=$("$cmake" -E sha256sum "$archive")
    printf '%s\n' "$checksum"
    if [[ ${checksum%% *} != b9c2df49707c57a48fc0923d52b8c73beb72d675f9d44b2211e4569be40a7421 ]]; then
        printf 'FlatBuffers archive checksum mismatch\n' >&2
        exit 1
    fi
    tar -xzf "$archive" -C "$work"
    fb="$work/flatbuffers-install"
    printf 'FlatBuffers build: %s\n' "$work/flatbuffers-build"
    "$cmake" -S "$work/flatbuffers-25.2.10" -B "$work/flatbuffers-build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_INSTALL_PREFIX="$fb" -DFLATBUFFERS_BUILD_TESTS=OFF \
        -DFLATBUFFERS_BUILD_FLATC=ON
    "$cmake" --build "$work/flatbuffers-build" --target install --parallel "$jobs"
else
    fb=$FLATBUFFERS_PREFIX
fi
printf 'FlatBuffers prefix: %s\n' "$fb"
"$fb/bin/flatc" --version

runtime_library=
codegen_tests=(flatbuffers_codegen_acceptance)
if [[ $runtime == ON ]]; then
    # Match the root GTest version without its unverified Git download path.
    archive="$work/googletest-release-1.12.0.tar.gz"
    curl --fail --location --retry 3 \
        https://github.com/google/googletest/archive/refs/tags/release-1.12.0.tar.gz \
        --output "$archive"
    checksum=$("$cmake" -E sha256sum "$archive")
    printf '%s\n' "$checksum"
    if [[ ${checksum%% *} != 2a4f11dce6188b256f3650061525d0fe352069e5c162452818efbbf8d0b5fe1c ]]; then
        printf 'GTest archive checksum mismatch\n' >&2
        exit 1
    fi
    tar -xzf "$archive" -C "$work"
    gtest="$work/googletest-release-1.12.0"
    printf 'GTest source (1.12.0): %s\nbRPC build: %s\n' "$gtest" "$work/brpc"
    "$cmake" -S "$root" -B "$work/brpc" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_CXX_COMPILER="$cxx" -DBUILD_SHARED_LIBS=ON \
        -DBUILD_UNIT_TESTS=ON -DDOWNLOAD_GTEST=OFF \
        -DBRPC_SYSTEM_GTEST_SOURCE_DIR="$gtest" \
        -DWITH_FLATBUFFERS=ON -DFLATBUFFERS_INCLUDE_DIR="$fb/include" \
        -DFLATBUFFERS_FLATC_EXECUTABLE="$fb/bin/flatc" \
        -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-}" \
        -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"
    "$cmake" --build "$work/brpc" --target brpc_flatbuffers_unittest --parallel "$jobs"
    # A registered CTest can still pass with zero GTest cases or all cases skipped.
    report=$(mktemp "$work/brpc/flatbuffers-gtest.XXXXXX")
    printf 'Core GTest XML: %s\n' "$report"
    (
        unset GTEST_TOTAL_SHARDS GTEST_SHARD_INDEX GTEST_SHARD_STATUS_FILE
        GTEST_FILTER='*' GTEST_REPEAT=1 GTEST_OUTPUT="xml:$report" \
            "$ctest" --test-dir "$work/brpc" --no-tests=error \
            -R '^brpc_flatbuffers_unittest$' --output-on-failure --verbose
    )
    if ! grep -Eq '<testsuites tests="[1-9][0-9]*" failures="0" disabled="0" errors="0"' "$report" ||
            grep -Eq '<skipped|status="notrun"|result="skipped"' "$report"; then
        printf 'Core FlatBuffers tests did not complete a nonempty, unskipped run: %s\n' "$report" >&2
        exit 1
    fi
    "$cmake" --build "$work/brpc" --target brpc-shared --parallel "$jobs"
    case $(uname -s) in
        Darwin) extension=dylib ;;
        *) extension=so ;;
    esac
    runtime_library="$work/brpc/output/lib/libbrpc.$extension"
    codegen_tests+=(flatbuffers_codegen_runtime)
else
    printf 'Compile-only codegen verification: skipping core and runtime tests\n'
fi
printf 'Codegen build: %s\n' "$work/codegen"
"$cmake" -S "$root/tools/flatbuffers" -B "$work/codegen" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}" \
    -DFLATBUFFERS_INCLUDE_DIR="$fb/include" \
    -DFLATBUFFERS_LIBRARY="$fb/lib/libflatbuffers.a" \
    -DFLATC_EXECUTABLE="$fb/bin/flatc" -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-}" \
    -DBRPC_CODEGEN_BRPC_LIBRARY="$runtime_library"
"$cmake" --build "$work/codegen" --parallel "$jobs"
"$work/codegen/brpc_flatc" --version
for test_name in "${codegen_tests[@]}"; do
    "$ctest" --test-dir "$work/codegen" --no-tests=error \
        -R "^${test_name}$" --output-on-failure --verbose
done
