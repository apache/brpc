#!/usr/bin/env sh
#
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
#

if [ "$SANITIZER" = "undefined" ]
then
    sed -i '/static void DoProfiling/i __attribute__((no_sanitize("undefined")))'   src/brpc/builtin/hotspots_service.cpp
    sed -i '/void PProfService::heap/i __attribute__((no_sanitize("undefined")))'   src/brpc/builtin/pprof_service.cpp
    sed -i '/void PProfService::growth/i __attribute__((no_sanitize("undefined")))' src/brpc/builtin/pprof_service.cpp
fi

mkdir -p build && cd build

cmake \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CFLAGS" -DCMAKE_CPP_FLAGS="$CFLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$CFLAGS" -DLIB_FUZZING_ENGINE="$LIB_FUZZING_ENGINE" \
    -DCMAKE_BUILD_TYPE=Debug -DBUILD_SHARED_LIBS=OFF -DWITH_SNAPPY=ON -DBUILD_UNIT_TESTS=ON -DBUILD_FUZZ_TESTS=ON ../.

# https://github.com/google/oss-fuzz/pull/10898
make \
    fuzz_butil fuzz_esp fuzz_hpack fuzz_http fuzz_hulu fuzz_json \
    fuzz_redis fuzz_shead fuzz_sofa fuzz_uri --ignore-errors -j$(nproc)

cp test/fuzz_* $OUT/

pushd /lib/x86_64-linux-gnu/
mkdir -p $OUT/lib/
cp libgflags* libprotobuf* libleveldb* libprotoc* libsnappy* $OUT/lib/.
popd

pushd $SRC/brpc/test/fuzzing
zip $OUT/fuzz_json_seed_corpus.zip  fuzz_json_seed_corpus/*
zip $OUT/fuzz_uri_seed_corpus.zip   fuzz_uri_seed_corpus/*
zip $OUT/fuzz_redis_seed_corpus.zip fuzz_redis_seed_corpus/*
zip $OUT/fuzz_http_seed_corpus.zip  fuzz_http_seed_corpus/*
zip $OUT/fuzz_butil_seed_corpus.zip fuzz_butil_seed_corpus/*
zip $OUT/fuzz_hpack_seed_corpus.zip fuzz_hpack_seed_corpus/*
popd
