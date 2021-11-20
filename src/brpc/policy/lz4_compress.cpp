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

#include <algorithm>

#include "butil/logging.h"
#include "butil/third_party/lz4/lz4.h"
#include "brpc/policy/lz4_compress.h"
#include "brpc/protocol.h"


namespace brpc {
namespace policy {

bool Lz4Compress(const google::protobuf::Message& res, butil::IOBuf* buf) {
    butil::IOBuf serialized_pb;
    butil::IOBufAsZeroCopyOutputStream wrapper(&serialized_pb);
    if (res.SerializeToZeroCopyStream(&wrapper)) {
        return Lz4Compress(serialized_pb, buf);
    }
    LOG(WARNING) << "Fail to serialize input pb=" << &res;
    return false;
}

bool Lz4Decompress(const butil::IOBuf& data, google::protobuf::Message* req) {
    butil::IOBuf binary_pb;
    if (Lz4Decompress(data, &binary_pb)) {
        return ParsePbFromIOBuf(req, binary_pb);
    }
    LOG(WARNING) << "Fail to lz4 decompress, size=" << data.size();
    return false;
}

bool Lz4Compress(const butil::IOBuf& in, butil::IOBuf* out) {
    size_t ref_cnt = in.backing_block_num(); 
    LZ4_stream_t* lz4_stream = LZ4_createStream();
    butil::IOBuf block_buf;
    std::vector<size_t> block_metas;
    for (size_t i = 0; i < ref_cnt; ++i) {
        butil::StringPiece block_view = in.backing_block(i);
        size_t src_block_size = block_view.size();
        size_t dst_block_bound = LZ4_compressBound(src_block_size);
        char* dst = new char[dst_block_bound];
        size_t dst_block_size =
            LZ4_compress_fast_continue(lz4_stream, block_view.data(), dst,
                                       src_block_size, dst_block_bound, 1);
        block_buf.append_user_data(reinterpret_cast<void *>(dst), dst_block_size,
                                   [](void *d) {
                                     char *nd = reinterpret_cast<char *>(d);
                                     delete[] nd;
                                   });
        block_metas.emplace_back(dst_block_size);
        block_metas.emplace_back(src_block_size);
    }
    size_t nblocks = block_metas.size() / 2;
    out->append(&nblocks, sizeof(size_t));
    out->append(block_metas.data(), sizeof(size_t) * block_metas.size());
    out->append(block_buf);
    LZ4_freeStream(lz4_stream);
    return true;
}

bool Lz4Decompress(const butil::IOBuf& in, butil::IOBuf* out) {
    butil::IOBufBytesIterator buf_iter(in);
    // nblocks 
    size_t in_size = in.size();
    if (buf_iter.bytes_left() < sizeof(size_t)) {
        LOG(ERROR) << "Invalid lz4 decompress buf, size=" << in_size;
        return false;
    }
    size_t nblocks = 0;
    buf_iter.copy_and_forward(&nblocks, sizeof(size_t));
    if (nblocks <= 0) {
        LOG(ERROR) << "Invalid nblocks=" << nblocks;
        return false;
    }

    // block_metas
    if (buf_iter.bytes_left() < nblocks * 2 * sizeof(size_t)) {
        LOG(ERROR) << "Invalid nblocks=" << nblocks
                   << " bytes_left=" << buf_iter.bytes_left();
        return false;
    }
    std::vector<size_t> block_metas(nblocks * 2, 0);
    buf_iter.copy_and_forward(block_metas.data(), nblocks * 2 * sizeof(size_t));
    size_t block_nbytes = 0;
    size_t max_block = 0; 
    for (size_t i = 0; i < nblocks; ++i) {
        block_nbytes += block_metas[2 * i];
        max_block = std::max(max_block, block_metas[2 * i]);
    }

    // block_buf
    if (buf_iter.bytes_left() != block_nbytes) {
        LOG(ERROR) << "Invalid block_nbytes=" << block_nbytes
                   << " bytes_left=" << buf_iter.bytes_left();
        return false;
    }
    LZ4_streamDecode_t* lz4_stream_decode = LZ4_createStreamDecode();
    char* in_scratch = new char[max_block];
    for (size_t i = 0; i < nblocks; ++i) {
        size_t src_block_size = block_metas[i * 2];
        size_t dst_block_size = block_metas[i * 2 + 1];
        char* out_buf = new char[dst_block_size];
        buf_iter.copy_and_forward(in_scratch, src_block_size);
        size_t dcp_size = LZ4_decompress_safe_continue(lz4_stream_decode, in_scratch, out_buf, src_block_size, dst_block_size);
        if (dcp_size != dst_block_size) {
            LOG(ERROR) << "Fail to lz4 decompress block, dst_block_size="
                       << dst_block_size << " decompress_size=" << dcp_size;
            delete[] out_buf;
            delete[] in_scratch;
            return false;
        }
        out->append_user_data(out_buf, dst_block_size, [](void *d) {
          char *nd = reinterpret_cast<char *>(d);
          delete[] nd;
        });
    }

    delete[] in_scratch;
    LZ4_freeStreamDecode(lz4_stream_decode);
    return true;
}

}  // namespace policy
} // namespace brpc
