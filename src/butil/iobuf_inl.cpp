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

// iobuf - A non-continuous zero-copied buffer

// Inlined implementations of some methods defined in iobuf.h

#include "butil/iobuf.h"
#include "butil/iobuf_inl.h"
#include "butil/iobuf_profiler.h"

namespace butil {

IOBuf::Block::Block(char* data_in, uint32_t data_size)
    : nshared(1)
    , flags(0)
    , abi_check(0)
    , size(0)
    , cap(data_size)
    , u({NULL})
    , data(data_in) {
    iobuf::g_nblock.fetch_add(1, butil::memory_order_relaxed);
    iobuf::g_blockmem.fetch_add(data_size + sizeof(Block),
                                butil::memory_order_relaxed);
    if (is_samplable()) {
        SubmitIOBufSample(this, 1);
    }
}

IOBuf::Block::Block(char* data_in, uint32_t data_size, UserDataDeleter deleter)
    : nshared(1)
    , flags(IOBUF_BLOCK_FLAGS_USER_DATA)
    , abi_check(0)
    , size(data_size)
    , cap(data_size)
    , u({0})
    , data(data_in) {
    auto ext = new (get_user_data_extension()) UserDataExtension();
    ext->deleter = std::move(deleter);
    if (is_samplable()) {
        SubmitIOBufSample(this, 1);
    }
}

UserDataExtension* IOBuf::Block::get_user_data_extension() {
    char* p = (char*)this;
    return (UserDataExtension*)(p + sizeof(Block));
}

inline void IOBuf::Block::check_abi() {
#ifndef NDEBUG
        if (abi_check != 0) {
            LOG(FATAL) << "Your program seems to wrongly contain two "
                "ABI-incompatible implementations of IOBuf";
        }
#endif
}

void IOBuf::Block::inc_ref() {
    check_abi();
    nshared.fetch_add(1, butil::memory_order_relaxed);
    if (sampled()) {
        SubmitIOBufSample(this, 1);
    }
}

void IOBuf::Block::dec_ref() {
    check_abi();
    if (sampled()) {
        SubmitIOBufSample(this, -1);
    }
    if (nshared.fetch_sub(1, butil::memory_order_release) == 1) {
        butil::atomic_thread_fence(butil::memory_order_acquire);
        if (!is_user_data()) {
            iobuf::g_nblock.fetch_sub(1, butil::memory_order_relaxed);
            iobuf::g_blockmem.fetch_sub(cap + sizeof(Block),
                                        butil::memory_order_relaxed);
            this->~Block();
            iobuf::blockmem_deallocate(this);
        } else if (flags & IOBUF_BLOCK_FLAGS_USER_DATA) {
            auto ext = get_user_data_extension();
            ext->deleter(data);
            ext->~UserDataExtension();
            this->~Block();
            free(this);
        }
    }
}

int IOBuf::Block::ref_count() const {
    return nshared.load(butil::memory_order_relaxed);
}

bool IOBuf::Block::is_samplable() {
    if (IsIOBufProfilerSamplable()) {
        flags |= IOBUF_BLOCK_FLAGS_SAMPLED;
        return true;
    }
    return false;
}

}   // namespace butil