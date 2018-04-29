// Copyright (c) 2018 Baidu Inc.
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

// Author: Li Zhaogeng (lizhaogeng01@baidu.com)

#ifndef BRPC_RDMATCMPOOL_CONST_H
#define BRPC_RDMATCMPOOL_CONST_H

#include <cstddef>

namespace brpc {
namespace rdma {
namespace tcmpool {

// If the length allocated is small than this value, thread cache will be used.
// Otherwise page heap will be used directly.
const size_t THRESHOLD_SMALL_OBJECT = 65536;

// The smallest block allocated one time.
const size_t MIN_OBJECT_SIZE = 64;

// The number of different block size in thread cache and central cache.
const int SIZE_CLASS_NUM = 11;

// The maximum page num in page heap list.
const int MAX_PAGE_NUM = 256;

// The size of a page in page heap.
const size_t PAGE_SIZE = 4096;

// The threshold to start a recycle from thread cache
const size_t RECYCLE_THRESHOLD = 2 * 1048576;

// The length of meta info before the allocated buffer.
const size_t META_LEN = sizeof(size_t);

}  // namespace tcmpool
}  // namespace rdma
}  // namespace brpc

#endif
