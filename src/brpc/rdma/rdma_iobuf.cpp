// Copyright (c) 2014 baidu-rpc authors.
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

// Author: Li,Zhaogeng (lizhaogeng01@baidu.com)

#ifdef BRPC_RDMA

#include "brpc/rdma/rdma_iobuf.h"

namespace brpc {
namespace rdma {

// refer to the value define in rdma_endpoint.cpp
static const size_t MAX_SGE = 6;

int RdmaIOBuf::get_block_sglist(ibv_sge* sglist, uint32_t lkey) const {
    int len = 0;
    const size_t nref = _ref_num();
    size_t num = (nref < MAX_SGE) ? nref : MAX_SGE;
    for (size_t i = 0; i < num; i++) {
        butil::IOBuf::BlockRef const& r = _ref_at(i);
        sglist[i].addr = (uint64_t)((char*)r.block + r.offset +
            butil::IOBuf::DEFAULT_BLOCK_SIZE - butil::IOBuf::DEFAULT_PAYLOAD);
        sglist[i].length = r.length;
        len += r.length;
        sglist[i].lkey = lkey;
    }
    return len;
}

}   // namespace rdma
}   // namespace brpc

#endif

