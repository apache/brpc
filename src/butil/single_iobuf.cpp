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

// SingleIOBuf - A continuous zero-copied buffer

#include "butil/logging.h"                  // CHECK, LOG
#include "butil/iobuf.h"
#include "butil/iobuf_inl.h"
#include "butil/single_iobuf.h"

namespace butil {

SingleIOBuf::SingleIOBuf()
    : _cur_block(NULL)
    , _block_size(0) {
    _cur_ref.offset = 0;
    _cur_ref.length = 0;
    _cur_ref.block = NULL;
}

SingleIOBuf::SingleIOBuf(const IOBuf::BlockRef& ref) {
    _cur_block = NULL;
    _block_size = 0;
    if (ref.block) {
        _cur_ref = ref;
        _cur_ref.block->inc_ref();
    }
}

SingleIOBuf::SingleIOBuf(const SingleIOBuf& other) {
    _cur_block = NULL;
    _block_size = 0;
    if (other._cur_ref.block != NULL) {
        _cur_ref = other._cur_ref;
        _cur_ref.block->inc_ref();
    }
}

SingleIOBuf::~SingleIOBuf() {
    reset();
}

SingleIOBuf& SingleIOBuf::operator=(const SingleIOBuf& rhs) {
    reset();
    _block_size = 0;
    if (rhs._cur_ref.block != NULL) {
        _cur_ref = rhs._cur_ref;
        _cur_ref.block->inc_ref();
    }
    return *this;
}

void SingleIOBuf::swap(SingleIOBuf& other) {
    if (this == &other) {
        return;
    }

    IOBuf::BlockRef tmp_ref = _cur_ref;
    _cur_ref = other._cur_ref;
    other._cur_ref = tmp_ref;
    IOBuf::Block* tmp_ptr = other._cur_block;
    other._cur_block = _cur_block;
    _cur_block = tmp_ptr;
    uint32_t tmp_size = other._block_size;
    other._block_size = _block_size;
    _block_size = tmp_size;
}

void* SingleIOBuf::allocate(uint32_t size) {
    IOBuf::Block* b = alloc_block_by_size(size);
    if (!b) {
        return NULL;
    }
    _cur_ref.offset = b->size;
    _cur_ref.length = size;
    _cur_ref.block = b;
    b->size += size;
    b->inc_ref();
    return b->data + _cur_ref.offset;
}

void SingleIOBuf::deallocate(void* p) {
    if (_cur_ref.block) {
        if (_cur_ref.block->data + _cur_ref.offset == (char*)p) { 
            reset();
        }
    }
}

IOBuf::Block* SingleIOBuf::alloc_block_by_size(uint32_t data_size) {
    if (_cur_block != NULL) {
        if (_cur_block->left_space() >= data_size) {
            return _cur_block;
        } else {
            _cur_block->dec_ref();
            _cur_block = NULL;
        }
    }
    uint32_t total_size = data_size + sizeof(IOBuf::Block);
    if (total_size <= IOBuf::DEFAULT_BLOCK_SIZE) {
        _cur_block = iobuf::acquire_tls_block();
        if (_cur_block != NULL) {
            if (_cur_block->left_space() >= data_size) {
                return _cur_block;
            } else {
                _cur_block->dec_ref();
                _cur_block = NULL;
            }
        }
        _cur_block = iobuf::create_block();
    } else {
        _cur_block = iobuf::create_block(total_size);
        _block_size = total_size;
    }
    if (BAIDU_UNLIKELY(!_cur_block)) {
        errno = ENOMEM;
        _block_size = 0;
        return NULL;
    }
    return _cur_block;
}

void SingleIOBuf::memcpy_downward(void* old_p, uint32_t old_size,
                    void* new_p, uint32_t new_size,
                    uint32_t in_use_back, uint32_t in_use_front) {
    memcpy((u_char*)new_p + new_size - in_use_back, (u_char*)old_p + old_size - in_use_back,
            in_use_back);
    memcpy(new_p, old_p, in_use_front);
}

void* SingleIOBuf::reallocate_downward(uint32_t new_size, uint32_t in_use_back,
                                        uint32_t in_use_front) {
    IOBuf::BlockRef& ref = _cur_ref;
    if (BAIDU_UNLIKELY(new_size <= ref.length)) {
        LOG(ERROR) << "invalid new size:" << new_size;
        errno = EINVAL;
        return NULL;
    }
    if (BAIDU_UNLIKELY(ref.block == NULL)) {
        LOG(ERROR) << "SingleIOBuf reallocate_downward failed. Block cannot be null!";
        errno = EINVAL;
        return NULL;
    }
    char* old_p = ref.block->data + ref.offset;
    uint32_t old_size = ref.length;
    IOBuf::Block* b = alloc_block_by_size(new_size);
    if (!b) {
        return NULL;
    }
    char* new_p = b->data + b->size;
    memcpy_downward(old_p, old_size,
                    new_p, new_size,
                    in_use_back, in_use_front);
    ref.block->dec_ref();
    _cur_ref.offset = b->size;
    _cur_ref.length = new_size;
    _cur_ref.block = b;
    b->size += new_size;
    b->inc_ref();
    return new_p;
}

const void* SingleIOBuf::get_begin() const {
    if (_cur_ref.block) {
        return _cur_ref.block->data + _cur_ref.offset;
    }
    return NULL;
}

uint32_t SingleIOBuf::get_length() const {
    return _cur_ref.length;
}

void SingleIOBuf::reset() {
    if (_cur_block) {
        if (_block_size == 0) {
            iobuf::release_tls_block(_cur_block);
        } else {
            _cur_block->dec_ref();
            _block_size = 0;
        }
        _cur_block = NULL;
    }
    if (_cur_ref.block != NULL) {
        _cur_ref.block->dec_ref();
    }
    _cur_ref.offset = 0;
    _cur_ref.length = 0;
    _cur_ref.block = NULL;
}

bool SingleIOBuf::assign(const IOBuf& buf, uint32_t msg_size) {
    if (BAIDU_UNLIKELY(buf.length() < msg_size)) {
        LOG(ERROR) << "Fail to dump_mult_iobuf msg_size:" << msg_size
            << " from source iobuf of size:" << buf.length();
        return false;
    }
    if (BAIDU_UNLIKELY(msg_size == 0)) {
        return true;
    }
    
    const IOBuf::BlockRef& ref = buf._front_ref();
    if (ref.length >= msg_size) {
        reset();
        _cur_ref.offset = ref.offset;
        _cur_ref.length = msg_size;
        _cur_ref.block = ref.block;
        _cur_ref.block->inc_ref();
        return true;
    } else {
        IOBuf::Block* b = alloc_block_by_size(msg_size);
        if (!b) {
            return false;
        }
        reset();
        char* out = b->data + b->size;
        const size_t nref = buf.backing_block_num();
        uint32_t last_len = msg_size;
        for (size_t i = 0; i < nref && last_len > 0; ++i) {
            const IOBuf::BlockRef& r = buf._ref_at(i);
            uint32_t n = std::min(r.length, last_len);
            iobuf::cp(out, r.block->data + r.offset, n);
            last_len -= n;
            out += n;
        }
        _cur_ref.offset = b->size;
        _cur_ref.length = msg_size;
        _cur_ref.block = b;
        b->size += msg_size;
        _cur_ref.block->inc_ref();
        return true;
    }
}

void SingleIOBuf::append_to(IOBuf* buf) const {
    if (buf && _cur_ref.block) {
        buf->_push_back_ref(_cur_ref);
    }
}

int SingleIOBuf::assign_user_data(void* data, size_t size, std::function<void(void*)> deleter) {
    if (size > 0xFFFFFFFFULL - 100) {
        LOG(FATAL) << "data_size=" << size << " is too large";
        return -1;
    }
    char* mem = (char*)malloc(sizeof(IOBuf::Block) + sizeof(UserDataExtension));
    if (mem == NULL) {
        return -1;
    }
    if (deleter == NULL) {
        deleter = ::free;
    }
    reset();
    IOBuf::Block* b = new (mem) IOBuf::Block((char*)data, size, deleter);
    _cur_ref.offset = 0;
    _cur_ref.length = b->cap;
    _cur_ref.block = b;
    return 0;
}

void SingleIOBuf::target_block_inc_ref(void* b) {
    IOBuf::Block* block = (IOBuf::Block*)b;
    block->inc_ref();
}

void SingleIOBuf::target_block_dec_ref(void* b) {
    IOBuf::Block* block = (IOBuf::Block*)b;
    block->dec_ref();
}

} // namespace butil