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

// Authors: Jiang,Lin (jianglin05@baidu.com)

#include <memory>

#include "butil/logging.h"
#include "butil/third_party/lz4/lz4.h"
#include "brpc/policy/lz4_compress.h"
#include "brpc/protocol.h"
#include "butil/third_party/snappy/snappy-internal.h"

using namespace butil;
using namespace std;
using butil::snappy::Varint;

namespace brpc {
namespace policy {

// This is a helper function for both LZ4Compress and LZ4Decompress to choose whether
// use the heap buffer of the buffer from IOBufAsZeroCopyOutputStream.
static char *PrepareOutputBuffer(IOBufAsZeroCopyOutputStream& ostream, int sizeRequired,
                                 unique_ptr<char[]>& bufferManager, int &noneHeapBufLen) {
    char *outputBuf = nullptr;
    int outputAvailableSize = 0;
    ostream.Next(reinterpret_cast<void**>(&outputBuf), &outputAvailableSize);
    if (outputAvailableSize >= sizeRequired) {
        noneHeapBufLen = outputAvailableSize;
        return outputBuf;
    } else {
        bufferManager.reset(new char[sizeRequired]);
        ostream.BackUp(outputAvailableSize);
        noneHeapBufLen = 0;
        return bufferManager.get();
    }
}

bool LZ4Compress(const butil::IOBuf& in, butil::IOBuf* out) {
    // Encode the raw data length into the output buffer.
    size_t len = in.length();
    char ulength[Varint::kMax32];
    char* p = Varint::Encode32(ulength, len);
    out->append(ulength, p - ulength);
    // Prepare the input buffer.
    // If the IOBuf contains all the data in one buffer, use it without memcpy.
    std::unique_ptr<char[]> inputBufManager;
    std::unique_ptr<char[]> outputBufManager;
    IOBufAsZeroCopyInputStream istream(in);
    int inputBufSize = 0;
    const char *inputBuf = nullptr;
    istream.Next(reinterpret_cast<const void**>(&inputBuf), &inputBufSize);
    const char *inputFullBuffer = nullptr;
    if (static_cast<size_t>(inputBufSize) == len) {
        inputFullBuffer = inputBuf;
    } else {
        inputBufManager.reset(new char[len]);
        int currentPos = 0;
        do {
            memcpy(inputBufManager.get() + currentPos, inputBuf, inputBufSize);
            currentPos += inputBufSize;
        } while (istream.Next(reinterpret_cast<const void**>(&inputBuf), &inputBufSize));
        inputFullBuffer = inputBufManager.get();
    }
    // Prepare the output buffer.
    int sizeBound = LZ4_compressBound(len);
    int noneHeapBufLen = 0;
    IOBufAsZeroCopyOutputStream ostream(out);
    char *outputFullBuffer = PrepareOutputBuffer(ostream, sizeBound, outputBufManager, noneHeapBufLen);
    // Do the compression.
    int compressLen = LZ4_compress_default(inputFullBuffer, outputFullBuffer, len, sizeBound);
    if (compressLen < 0) {
        return false;
    }
    if (noneHeapBufLen > 0) {
        // This buffer is from output IOBuf.
        ostream.BackUp(noneHeapBufLen - compressLen);
    } else {
        // This buffer is from heap.
        out->append(outputFullBuffer, compressLen);
    }
    return true;
}

bool LZ4Decompress(const butil::IOBuf& in, butil::IOBuf* out) {
    int len = in.length();
    int inputBufSize = 0;
    const char *inputBuf = nullptr;
    IOBufAsZeroCopyInputStream istream(in);
    istream.Next(reinterpret_cast<const void**>(&inputBuf), &inputBufSize);
    // Decode the decompression size. The var-int may be stored cross buffers.
    uint32_t decodingSize = 0;
    uint32_t varIntShift = 0;
    const char* start = inputBuf;
    bool finish = false;
    int previousBufLen = 0;
    while (true) {
        if (varIntShift >= 32) return false;
        for (int i = 0; i < inputBufSize; ++i) {
            const unsigned char c = *(reinterpret_cast<const unsigned char*>(start++));
            decodingSize |= static_cast<uint32_t>(c & 0x7f) << varIntShift;
            if (c < 128) {
                finish = true;
                break;
            }
            varIntShift += 7;
        }
        if (finish) {
            break;
        }
        previousBufLen += start - inputBuf;
        istream.Next(reinterpret_cast<const void**>(&inputBuf), &inputBufSize);
        start = inputBuf;
    }
    int lengthOffset = start - inputBuf;
    std::unique_ptr<char[]> inputBufManager;
    std::unique_ptr<char[]> outputBufManager;
    // Prepare the input buffer.
    const char *inputFullBuffer = nullptr;
    if (inputBufSize + previousBufLen == len) {
        inputFullBuffer = start;
    } else {
        inputBufManager.reset(new char[len]);
        int currentPos = 0;
        int curBufLen = inputBufSize - lengthOffset;
        do {
            memcpy(inputBufManager.get() + currentPos, start, curBufLen);
            currentPos += curBufLen;
        } while (istream.Next(reinterpret_cast<const void**>(&start), &curBufLen));
        inputFullBuffer = inputBufManager.get();
    }
    // Prepare the output buffer.
    int noneHeapBufLen = 0;
    IOBufAsZeroCopyOutputStream ostream(out);
    char *outputFullBuffer = PrepareOutputBuffer(ostream, decodingSize, outputBufManager, noneHeapBufLen);
    // Decompress.
    int decompressLen = LZ4_decompress_safe(inputFullBuffer, outputFullBuffer, len - lengthOffset - previousBufLen, decodingSize);
    if (decompressLen <= 0) {
        return false;
    }
    if (noneHeapBufLen > 0) {
        ostream.BackUp(noneHeapBufLen - decompressLen);
    } else {
        out->append(outputFullBuffer, decompressLen);
    }
    return true;
}

bool LZ4Compress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (res.SerializeToZeroCopyStream(&wrapper)) {
        return LZ4Compress(serialized_pb, buf);
    }
    LOG(WARNING) << "LZ4Compress: Fail to serialize input pb=" << &res;
    return false;
}

bool LZ4Decompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    butil::IOBufAsSnappySource source(data);
    butil::IOBuf binary_pb;
    if (LZ4Decompress(data, &binary_pb)) {
        return ParsePbFromIOBuf(req, binary_pb);
    }
    LOG(WARNING) << "Fail to LZ4Decompress, size=" << data.size();
    return false;
}

} // namespace policy
} // namespace brpc
