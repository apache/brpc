// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to You under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and
// limitations under the License.

// Fuzz brpc's compression policies on the wire direction that servers are
// attacked through: fuzz bytes are compressed with gzip/zlib (both window
// styles) or snappy, and arbitrary fuzz bytes are also fed straight into the
// decompressors (the hostile-compressed-payload model).

#include "butil/iobuf.h"
#include "brpc/policy/gzip_compress.h"
#include "brpc/policy/snappy_compress.h"

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 5) {
        return 0;
    }

    const uint8_t mode = data[0] % 5;
    butil::IOBuf in;
    in.append(data + 1, size - 1);
    butil::IOBuf mid;
    butil::IOBuf out;

    brpc::policy::GzipCompressOptions opts;
    brpc::policy::GzipCompressOptions zlib_opts;
    zlib_opts.format = google::protobuf::io::GzipOutputStream::ZLIB;
    brpc::policy::GzipCompressOptions fast_opts;
    fast_opts.compression_level = 1;

    switch (mode) {
    case 0: {
        // gzip round-trip
        if (brpc::policy::GzipCompress(in, &mid, &opts)) {
            brpc::policy::GzipDecompress(mid, &out);
        }
        break;
    }
    case 1: {
        // zlib round-trip
        if (brpc::policy::GzipCompress(in, &mid, &zlib_opts)) {
            brpc::policy::ZlibDecompress(mid, &out);
        }
        break;
    }
    case 2: {
        // gzip with a different compression level
        if (brpc::policy::GzipCompress(in, &mid, &fast_opts)) {
            brpc::policy::GzipDecompress(mid, &out);
        }
        break;
    }
    case 3: {
        // snappy round-trip
        if (brpc::policy::SnappyCompress(in, &mid)) {
            brpc::policy::SnappyDecompress(mid, &out);
        }
        break;
    }
    case 4: {
        // hostile compressed payload: feed the raw fuzz bytes to every
        // decompressor
        brpc::policy::GzipDecompress(in, &out);
        out.clear();
        brpc::policy::SnappyDecompress(in, &out);
        break;
    }
    }
    return 0;
}
