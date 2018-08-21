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
            butil::IOBuf in(data);

            LZ4_stream_t lz4Stream_body;
            LZ4_stream_t* lz4Stream = &lz4Stream_body;

            char inpBuf[2][BLOCK_BYTES];
            int  inpBufIndex = 0;

            LZ4_resetStream(lz4Stream);

            for (;;) {
                char* const inpPtr = inpBuf[inpBufIndex];
                const int inpBytes = in.cutn(inpPtr,BLOCK_BYTES);
                if (0 == inpBytes) {
                    break;
                }

                {
                    char cmpBuf[LZ4_COMPRESSBOUND(BLOCK_BYTES)];
                    const int cmpBytes = LZ4_compress_fast_continue(
                            lz4Stream, inpPtr, cmpBuf, inpBytes, sizeof(cmpBuf), 1);
                    if (cmpBytes <= 0) {
                        LOG(WARNING) << "LZ4 Compress failed";
                        return false;
                    }
                    out->append(&cmpBytes, sizeof(cmpBytes));
                    out->append(&cmpBuf,cmpBytes);
                }


                inpBufIndex = (inpBufIndex + 1) % 2;
            }

            const int end = 0;
            out->append(&end, sizeof(end));

            return true;
        }

        bool LZ4Decompress(const butil::IOBuf& data, butil::IOBuf* out) {
            butil::IOBuf in(data);

            LZ4_streamDecode_t lz4StreamDecode_body;
            LZ4_streamDecode_t* lz4StreamDecode = &lz4StreamDecode_body;
            LZ4_setStreamDecode(lz4StreamDecode, NULL, 0);

            char decBuf[2][BLOCK_BYTES];
            int  decBufIndex = 0;


            for(;;) {
                char cmpBuf[LZ4_COMPRESSBOUND(BLOCK_BYTES)];
                int  cmpBytes = 0;

                {
                    in.cutn(&cmpBytes, sizeof(int));
                    if(cmpBytes <= 0) {
                        break;
                    }

                    in.cutn(&cmpBuf,cmpBytes);
                }

                {
                    char* const decPtr = decBuf[decBufIndex];
                    const int decBytes = LZ4_decompress_safe_continue(
                            lz4StreamDecode, cmpBuf, decPtr, cmpBytes, BLOCK_BYTES);
                    if(decBytes <= 0) {
                        LOG(WARNING) << "LZ4 Uncompress failed";
                        return false;
                    }
                    out->append(decPtr,decBytes);
                }

                decBufIndex = (decBufIndex + 1) % 2;
            }

            return true;
        }


        bool LZ4Compress(const google::protobuf::Message& res, butil::IOBuf* buf) {
            butil::IOBuf in;
            butil::IOBufAsZeroCopyOutputStream wrapper(&in);

            if (res.SerializeToZeroCopyStream(&wrapper)) {
                return LZ4Compress(in,buf);
            }

            LOG(WARNING) << "Fail to serialize input pb=" << &res;
            return false;
        }

        bool LZ4Decompress(const butil::IOBuf& data, google::protobuf::Message* req) {
            butil::IOBuf out;
            if(LZ4Decompress(data,&out) && ParsePbFromIOBuf(req,out)) {
                return true;
            }

            LOG(WARNING) << "Fail to lz4::Uncompress, size=" << data.size();
            return false;
        }


    }  // namespace policy
} // namespace