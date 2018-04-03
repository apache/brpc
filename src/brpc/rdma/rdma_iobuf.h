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

#ifndef BRPC_RDMA_IOBUF_H
#define BRPC_RDMA_IOBUF_H

#include <infiniband/verbs.h>
#include <butil/iobuf.h>

namespace brpc {
namespace rdma {

class RdmaEndpoint;

// RdmaIOBuf inherits from IOBuf to provide a function: get_block_sglist.
// The reason is that we need to use some protected member function of IOBuf.
class RdmaIOBuf : public butil::IOBuf {
friend class RdmaEndpoint;
private:
    // Generate ibv_sge (addr, length, lkey) for all blocks.
    // The @a sglist is an ibv_sge array (len>=16) which should be allocated
    // before calling this function.
    //
    // Return: the bytes included in the sglist
    int get_block_sglist(ibv_sge* sglist, uint32_t lkey) const;
};

}   // namespace rdma
}   // namespace brpc

#endif   // BRPC_RDMA_IOBUF_H

#endif

