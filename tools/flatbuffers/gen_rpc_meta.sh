#!/usr/bin/env bash
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# Regenerate src/brpc/policy/flatbuffers_rpc_meta_generated.h, or with
# --check, fail if the checked-in header differs from what flatc generates.
#
# Usage: tools/flatbuffers/gen_rpc_meta.sh [--check] [path/to/flatc]
# flatc must be the official compiler of the supported FlatBuffers version.

set -euo pipefail

check=0
if [ "${1:-}" = "--check" ]; then
    check=1
    shift
fi
flatc="${1:-${FLATC_EXECUTABLE:-flatc}}"

root="$(cd "$(dirname "$0")/../.." && pwd)"
dir="$root/src/brpc/policy"
schema="$dir/flatbuffers_rpc_meta.fbs"
header="$dir/flatbuffers_rpc_meta_generated.h"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$flatc" --cpp -o "$tmp" "$schema"
# The license header of the schema is copied to the generated header.
{
    sed -n '1,/^$/p' "$schema"
    cat "$tmp/flatbuffers_rpc_meta_generated.h"
} > "$tmp/out.h"

if [ "$check" = 1 ]; then
    if ! diff -u "$header" "$tmp/out.h"; then
        echo "$header is stale; run tools/flatbuffers/gen_rpc_meta.sh" >&2
        exit 1
    fi
    echo "$header is up to date"
else
    cp "$tmp/out.h" "$header"
    echo "Wrote $header"
fi
