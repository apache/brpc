// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "brpc/amf.h"
#include "butil/iobuf.h"

#define kMinInputLength 5
#define kMaxInputLength 4096

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < kMinInputLength || size > kMaxInputLength){
        return 1;
    }

    uint8_t mode = data[0] % 3;
    const uint8_t *payload = data + 1;
    size_t payload_size = size - 1;

    butil::IOBuf buf;
    buf.append(payload, payload_size);

    switch (mode) {
        case 0: {
            // Read AMF object
            butil::IOBufAsZeroCopyInputStream zc_stream(buf);
            brpc::AMFInputStream stream(&zc_stream);
            brpc::AMFObject obj;
            brpc::ReadAMFObject(&obj, &stream);
            break;
        }
        case 1: {
            // Read AMF string
            butil::IOBufAsZeroCopyInputStream zc_stream(buf);
            brpc::AMFInputStream stream(&zc_stream);
            std::string val;
            brpc::ReadAMFString(&val, &stream);
            break;
        }
        case 2: {
            // Read raw AMF fields by consuming the stream directly
            butil::IOBufAsZeroCopyInputStream zc_stream(buf);
            brpc::AMFInputStream stream(&zc_stream);
            uint8_t marker;
            while (stream.good() && stream.cut_u8(&marker) == 1) {
                // Try to identify marker type and read value
                if (marker == brpc::AMF_MARKER_NUMBER) {
                    uint64_t num;
                    stream.cut_u64(&num);
                } else if (marker == brpc::AMF_MARKER_BOOLEAN) {
                    uint8_t b;
                    stream.cut_u8(&b);
                } else if (marker == brpc::AMF_MARKER_STRING) {
                    uint16_t len;
                    if (stream.cut_u16(&len) == 2 && len < 1024) {
                        char tmp[1024];
                        stream.cutn(tmp, len);
                    }
                }
            }
            break;
        }
    }

    return 0;
}
