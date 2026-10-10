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

uname -sm
"$cxx" --version
"$cmake" --version
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
    if [[ ${checksum%% *} != b9c2df49707c57a48fc0923d52b8c73beb72d675f9d44b2211e4569be40a7421 ]]; then
        printf 'FlatBuffers archive checksum mismatch\n' >&2
        exit 1
    fi
    tar -xzf "$archive" -C "$work"
    fb="$work/flatbuffers-install"
    "$cmake" -S "$work/flatbuffers-25.2.10" -B "$work/flatbuffers-build" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="$cxx" \
        -DCMAKE_INSTALL_PREFIX="$fb" -DFLATBUFFERS_BUILD_TESTS=OFF \
        -DFLATBUFFERS_BUILD_FLATC=ON
    "$cmake" --build "$work/flatbuffers-build" --target install --parallel "$jobs"
else
    fb=$FLATBUFFERS_PREFIX
fi
"$fb/bin/flatc" --version

runtime_args=()
if [[ ${CODEGEN_RUNTIME:-ON} == ON ]]; then
    "$cmake" -S "$root" -B "$work/brpc" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_CXX_COMPILER="$cxx" -DBUILD_SHARED_LIBS=ON \
        -DWITH_FLATBUFFERS=ON -DFLATBUFFERS_INCLUDE_DIR="$fb/include" \
        -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-}" \
        -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}"
    "$cmake" --build "$work/brpc" --target brpc-shared --parallel "$jobs"
    case $(uname -s) in
        Darwin) extension=dylib ;;
        *) extension=so ;;
    esac
    runtime_args+=("-DBRPC_CODEGEN_BRPC_LIBRARY=$work/brpc/output/lib/libbrpc.$extension")
fi
"$cmake" -S "$root/tools/flatbuffers" -B "$work/codegen" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_PREFIX_PATH="${CMAKE_PREFIX_PATH:-}" \
    -DFLATBUFFERS_INCLUDE_DIR="$fb/include" \
    -DFLATBUFFERS_LIBRARY="$fb/lib/libflatbuffers.a" \
    -DFLATC_EXECUTABLE="$fb/bin/flatc" -DOPENSSL_ROOT_DIR="${OPENSSL_ROOT_DIR:-}" \
    "${runtime_args[@]}"
"$cmake" --build "$work/codegen" --parallel "$jobs"
"$work/codegen/brpc_flatc" --version
"$ctest" --test-dir "$work/codegen" --no-tests=error \
    -R '^flatbuffers_codegen_(acceptance|runtime)$' --output-on-failure
