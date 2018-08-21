// Copyright (c) 2018 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: HaoPeng,Li (happenlee@hotmail.com)

#include "butil/logging.h"
#include "butil/third_party/lz4/lz4.h"
#include "brpc/policy/lz4_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool LZ4Compress(const butil::IOBuf& data, butil::IOBuf* out) {
    butil::IOBufBytesIterator in(data);

    butil::lz4::LZ4_stream_t lz4_stream_body;
    butil::lz4::LZ4_stream_t* lz4_stream = &lz4_stream_body;

    char inp_buf[2][BLOCK_BYTES];
    int  inp_buf_index = 0;

    butil::lz4::LZ4_resetStream(lz4_stream);

    for (;;) {
        char* const inp_ptr = inp_buf[inp_buf_index];
        const int inp_bytes = in.copy_and_forward(inp_ptr, BLOCK_BYTES);

        if (0 == inp_bytes) {
            break;
        }

        {
            char cmp_buf[LZ4_COMPRESSBOUND(BLOCK_BYTES)];
            const int cmp_bytes = butil::lz4::LZ4_compress_fast_continue(
                                     lz4_stream, inp_ptr, cmp_buf, inp_bytes, sizeof(cmp_buf), 1);

            if (cmp_bytes <= 0) {
                LOG(WARNING) << "LZ4 Compress failed";
                return false;
            }

            out->append(&cmp_bytes, sizeof(cmp_bytes));
            out->append(&cmp_buf, cmp_bytes);
        }


        inp_buf_index = (inp_buf_index + 1) % 2;
    }

    const int end = 0;
    out->append(&end, sizeof(end));

    return true;
}

bool LZ4Decompress(const butil::IOBuf& data, butil::IOBuf* out) {
    butil::IOBufBytesIterator in(data);

    butil::lz4::LZ4_streamDecode_t lz4_stream_decode_body;
    butil::lz4::LZ4_streamDecode_t* lz4_stream_decode = &lz4_stream_decode_body;
    butil::lz4::LZ4_setStreamDecode(lz4_stream_decode, NULL, 0);

    //Why does LZ4 use two cache blocks? You can refer to the following information.
    //https://github.com/lz4/lz4/blob/dev/examples/blockStreaming_doubleBuffer.md
    char dec_buf[2][BLOCK_BYTES];
    int  dec_buf_index = 0;


    for (;;) {
        char cmp_buf[LZ4_COMPRESSBOUND(BLOCK_BYTES)];
        int  cmp_bytes = 0;

        {
            in.copy_and_forward(&cmp_bytes, sizeof(int));

            if (cmp_bytes <= 0) {
                break;
            }

            in.copy_and_forward(&cmp_buf, cmp_bytes);
        }

        {
            char* const dec_ptr = dec_buf[dec_buf_index];
            const int dec_bytes = butil::lz4::LZ4_decompress_safe_continue(
                                     lz4_stream_decode, cmp_buf, dec_ptr, cmp_bytes, BLOCK_BYTES);

            if (dec_bytes <= 0) {
                LOG(WARNING) << "LZ4 Uncompress failed";
                return false;
            }

            out->append(dec_ptr, dec_bytes);
        }

        dec_buf_index = (dec_buf_index + 1) % 2;
    }

    return true;
}


bool LZ4Compress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    butil::IOBuf in;
    butil::IOBufAsZeroCopyOutputStream wrapper(&in);

    if (res.SerializeToZeroCopyStream(&wrapper)) {
        return LZ4Compress(in, buf);
    }

    LOG(WARNING) << "Fail to serialize input pb=" << &res;
    return false;
}

bool LZ4Decompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    butil::IOBuf out;

    if (LZ4Decompress(data, &out) && ParsePbFromIOBuf(req, out)) {
        return true;
    }

    LOG(WARNING) << "Fail to lz4::Uncompress, size=" << data.size();
    return false;
}


}  // namespace policy
} // namespace