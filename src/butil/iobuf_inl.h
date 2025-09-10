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

// Date: Thu Nov 22 13:57:56 CST 2012

// Inlined implementations of some methods defined in iobuf.h

#ifndef BUTIL_IOBUF_INL_H
#define BUTIL_IOBUF_INL_H

#include "butil/atomicops.h"                // butil::atomic
#include "butil/thread_local.h"             // thread_atexit
#include "butil/logging.h"                  // CHECK, LOG

void* fast_memcpy(void *__restrict dest, const void *__restrict src, size_t n);

namespace butil {

using UserDataDeleter = std::function<void(void*)>;

struct UserDataExtension {
    UserDataDeleter deleter;
};

bool IsIOBufProfilerSamplable();
void SubmitIOBufSample(IOBuf::Block* block, int64_t ref);

const uint16_t IOBUF_BLOCK_FLAGS_USER_DATA = 1 << 0;
const uint16_t IOBUF_BLOCK_FLAGS_SAMPLED = 1 << 1;

inline ssize_t IOBuf::cut_into_file_descriptor(int fd, size_t size_hint) {
    return pcut_into_file_descriptor(fd, -1, size_hint);
}

inline ssize_t IOBuf::cut_multiple_into_file_descriptor(
    int fd, IOBuf* const* pieces, size_t count) {
    return pcut_multiple_into_file_descriptor(fd, -1, pieces, count);
}

inline int IOBuf::append_user_data(void* data, size_t size, std::function<void(void*)> deleter) {
    return append_user_data_with_meta(data, size, std::move(deleter), 0);
}

inline ssize_t IOPortal::append_from_file_descriptor(int fd, size_t max_count) {
    return pappend_from_file_descriptor(fd, -1, max_count);
}

inline void IOPortal::return_cached_blocks() {
    if (_block) {
        return_cached_blocks_impl(_block);
        _block = NULL;
    }
}

inline void reset_block_ref(IOBuf::BlockRef& ref) {
    ref.offset = 0;
    ref.length = 0;
    ref.block = NULL;
}

inline IOBuf::IOBuf() {
    reset_block_ref(_sv.refs[0]);
    reset_block_ref(_sv.refs[1]);
}

inline IOBuf::IOBuf(const Movable& rhs) {
    _sv = rhs.value()._sv;
    new (&rhs.value()) IOBuf;
}

inline void IOBuf::operator=(const Movable& rhs) {
    clear();
    _sv = rhs.value()._sv;
    new (&rhs.value()) IOBuf;
}

inline void IOBuf::operator=(const char* s) {
    clear();
    append(s);
}

inline void IOBuf::operator=(const std::string& s) {
    clear();
    append(s);
}

inline void IOBuf::swap(IOBuf& other) {
    const SmallView tmp = other._sv;
    other._sv = _sv;
    _sv = tmp;
}

inline int IOBuf::cut_until(IOBuf* out, char const* delim) {
    if (*delim) {
        if (!*(delim+1)) {
            return _cut_by_char(out, *delim);
        } else {
            return _cut_by_delim(out, delim, strlen(delim));
        }
    }
    return -1;
}

inline int IOBuf::cut_until(IOBuf* out, const std::string& delim) {
    if (delim.length() == 1UL) {
        return _cut_by_char(out, delim[0]);
    } else if (delim.length() > 1UL) {
        return _cut_by_delim(out, delim.data(), delim.length());
    } else {
        return -1;
    }
}

inline int IOBuf::append(const std::string& s) {
    return append(s.data(), s.length());
}

inline std::string IOBuf::to_string() const {
    std::string s;
    copy_to(&s);
    return s;
}

inline bool IOBuf::empty() const {
    return _small() ? !_sv.refs[0].block : !_bv.nbytes;
}

inline size_t IOBuf::length() const {
    return _small() ?
        (_sv.refs[0].length + _sv.refs[1].length) : _bv.nbytes;
}

inline bool IOBuf::_small() const {
    return _bv.magic >= 0;
}

inline size_t IOBuf::_ref_num() const {
    return _small()
        ? (!!_sv.refs[0].block + !!_sv.refs[1].block) : _bv.nref;
}

inline IOBuf::BlockRef& IOBuf::_front_ref() {
    return _small() ? _sv.refs[0] : _bv.refs[_bv.start];
}

inline const IOBuf::BlockRef& IOBuf::_front_ref() const {
    return _small() ? _sv.refs[0] : _bv.refs[_bv.start];
}

inline IOBuf::BlockRef& IOBuf::_back_ref() {
    return _small() ? _sv.refs[!!_sv.refs[1].block] : _bv.ref_at(_bv.nref - 1);
}

inline const IOBuf::BlockRef& IOBuf::_back_ref() const {
    return _small() ? _sv.refs[!!_sv.refs[1].block] : _bv.ref_at(_bv.nref - 1);
}

inline IOBuf::BlockRef& IOBuf::_ref_at(size_t i) {
    return _small() ? _sv.refs[i] : _bv.ref_at(i);
}

inline const IOBuf::BlockRef& IOBuf::_ref_at(size_t i) const {
    return _small() ? _sv.refs[i] : _bv.ref_at(i);
}

inline const IOBuf::BlockRef* IOBuf::_pref_at(size_t i) const {
    if (_small()) {
        return i < (size_t)(!!_sv.refs[0].block + !!_sv.refs[1].block) ? &_sv.refs[i] : NULL;
    } else {
        return i < _bv.nref ? &_bv.ref_at(i) : NULL;
    }
}

inline bool operator==(const IOBuf::BlockRef& r1, const IOBuf::BlockRef& r2) {
    return r1.offset == r2.offset && r1.length == r2.length &&
        r1.block == r2.block;
}
        
inline bool operator!=(const IOBuf::BlockRef& r1, const IOBuf::BlockRef& r2) {
    return !(r1 == r2);
}

inline void IOBuf::_push_back_ref(const BlockRef& r) {
    if (_small()) {
        return _push_or_move_back_ref_to_smallview<false>(r);
    } else {
        return _push_or_move_back_ref_to_bigview<false>(r);
    }
}

inline void IOBuf::_move_back_ref(const BlockRef& r) {
    if (_small()) {
        return _push_or_move_back_ref_to_smallview<true>(r);
    } else {
        return _push_or_move_back_ref_to_bigview<true>(r);
    }
}

////////////////  IOBufCutter ////////////////
inline size_t IOBufCutter::remaining_bytes() const {
    if (_block) {
        return (char*)_data_end - (char*)_data + _buf->size() - _buf->_front_ref().length;
    } else {
        return _buf->size();
    }
}

inline bool IOBufCutter::cut1(void* c) {
    if (_data == _data_end) {
        if (!load_next_ref()) {
            return false;
        }
    }
    *(char*)c = *(const char*)_data;
    _data = (char*)_data + 1;
    return true;
}

inline const void* IOBufCutter::fetch1() {
    if (_data == _data_end) {
        if (!load_next_ref()) {
            return NULL;
        }
    }
    return _data;
}

inline size_t IOBufCutter::copy_to(void* out, size_t n) {
    size_t size = (char*)_data_end - (char*)_data;
    if (n <= size) {
        memcpy(out, _data, n);
        return n;
    }
    return slower_copy_to(out, n);
}

inline size_t IOBufCutter::pop_front(size_t n) {
    const size_t saved_n = n;
    do {
        const size_t size = (char*)_data_end - (char*)_data;
        if (n <= size) {
            _data = (char*)_data + n;
            return saved_n;
        }
        n -= size;
        if (!load_next_ref()) {
            return saved_n - n;
        }
    } while (true);
}

inline size_t IOBufCutter::cutn(std::string* out, size_t n) {
    if (n == 0) {
        return 0;
    }
    const size_t len = remaining_bytes();
    if (n > len) {
        n = len;
    }
    const size_t old_size = out->size();
    out->resize(out->size() + n);
    return cutn(&(*out)[old_size], n);
}

/////////////// IOBufAppender /////////////////
inline int IOBufAppender::append(const void* src, size_t n) {
    do {
        const size_t size = (char*)_data_end - (char*)_data;
        if (n <= size) {
            memcpy(_data, src, n);
            _data = (char*)_data + n;
            return 0;
        }
        if (size != 0) {
            memcpy(_data, src, size);
            src = (const char*)src + size;
            n -= size;
        }
        if (add_block() != 0) {
            return -1;
        }
    } while (true);
}

inline int IOBufAppender::append(const StringPiece& str) {
    return append(str.data(), str.size());
}

inline int IOBufAppender::append_decimal(long d) {
    char buf[24];  // enough for decimal 64-bit integers
    size_t n = sizeof(buf);
    bool negative = false;
    if (d < 0) {
        negative = true;
        d = -d;
    }
    do {
        const long q = d / 10;
        buf[--n] = d - q * 10 + '0';
        d = q;
    } while (d);
    if (negative) {
        buf[--n] = '-';
    }
    return append(buf + n, sizeof(buf) - n);
}

inline int IOBufAppender::push_back(char c) {
    if (_data == _data_end) {
        if (add_block() != 0) {
            return -1;
        }
    }
    char* const p = (char*)_data;
    *p = c;
    _data = p + 1;
    return 0;
}

inline int IOBufAppender::add_block() {
    int size = 0;
    if (_zc_stream.Next(&_data, &size)) {
        _data_end = (char*)_data + size;
        return 0;
    }
    _data = NULL;
    _data_end = NULL;
    return -1;
}

inline void IOBufAppender::shrink() {
    const size_t size = (char*)_data_end - (char*)_data;
    if (size != 0) {
        _zc_stream.BackUp(size);
        _data = NULL;
        _data_end = NULL;
    }
}

inline IOBufBytesIterator::IOBufBytesIterator(const butil::IOBuf& buf)
    : _block_begin(NULL), _block_end(NULL), _block_count(0), 
      _bytes_left(buf.length()), _buf(&buf) {
    try_next_block();
}

inline IOBufBytesIterator::IOBufBytesIterator(const IOBufBytesIterator& it)
    : _block_begin(it._block_begin)
    , _block_end(it._block_end)
    , _block_count(it._block_count)
    , _bytes_left(it._bytes_left)
    , _buf(it._buf) {
}

inline IOBufBytesIterator::IOBufBytesIterator(
    const IOBufBytesIterator& it, size_t bytes_left)
    : _block_begin(it._block_begin)
    , _block_end(it._block_end)
    , _block_count(it._block_count)
    , _bytes_left(bytes_left)
    , _buf(it._buf) {
    //CHECK_LE(_bytes_left, it._bytes_left);
    if (_block_end > _block_begin + _bytes_left) {
        _block_end = _block_begin + _bytes_left;
    }
}

inline void IOBufBytesIterator::try_next_block() {
    if (_bytes_left == 0) {
        return;
    }
    butil::StringPiece s = _buf->backing_block(_block_count++);
    _block_begin = s.data();
    _block_end = s.data() + std::min(s.size(), (size_t)_bytes_left);
}

inline void IOBufBytesIterator::operator++() {
    ++_block_begin;
    --_bytes_left;
    if (_block_begin == _block_end) {
        try_next_block();
    }
}

inline size_t IOBufBytesIterator::copy_and_forward(void* buf, size_t n) {
    size_t nc = 0;
    while (nc < n && _bytes_left != 0) {
        const size_t block_size = _block_end - _block_begin;
        const size_t to_copy = std::min(block_size, n - nc);
        memcpy((char*)buf + nc, _block_begin, to_copy);
        _block_begin += to_copy;
        _bytes_left -= to_copy;
        nc += to_copy;
        if (_block_begin == _block_end) {
            try_next_block();
        }
    }
    return nc;
}

inline size_t IOBufBytesIterator::copy_and_forward(std::string* s, size_t n) {
    bool resized = false;
    if (s->size() < n) {
        resized = true;
        s->resize(n);
    }
    const size_t nc = copy_and_forward(const_cast<char*>(s->data()), n);
    if (nc < n && resized) {
        s->resize(nc);
    }
    return nc;
}

inline size_t IOBufBytesIterator::forward(size_t n) {
    size_t nc = 0;
    while (nc < n && _bytes_left != 0) {
        const size_t block_size = _block_end - _block_begin;
        const size_t to_copy = std::min(block_size, n - nc);
        _block_begin += to_copy;
        _bytes_left -= to_copy;
        nc += to_copy;
        if (_block_begin == _block_end) {
            try_next_block();
        }
    }
    return nc;
}

// Used by max_blocks_per_thread()
bool IsIOBufProfilerEnabled();

namespace iobuf {
void inc_g_nblock();
void dec_g_nblock();

void inc_g_blockmem();
void dec_g_blockmem();

void inc_g_num_hit_tls_threshold();
void dec_g_num_hit_tls_threshold();

// Function pointers to allocate or deallocate memory for a IOBuf::Block
extern void* (*blockmem_allocate)(size_t);
extern void  (*blockmem_deallocate)(void*);

} // namespace iobuf

struct IOBuf::Block {
    butil::atomic<int> nshared;
    uint16_t flags;
    uint16_t abi_check;  // original cap, never be zero.
    uint32_t size;
    uint32_t cap;
    // When flag is 0, portal_next is valid.
    // When flag & IOBUF_BLOCK_FLAGS_USER_DATA is non-0, data_meta is valid.
    union {
        Block* portal_next;
        uint64_t data_meta;
    } u;
    // When flag is 0, data points to `size` bytes starting at `(char*)this+sizeof(Block)'
    // When flag & IOBUF_BLOCK_FLAGS_USER_DATA is non-0, data points to the user data and
    // the deleter is put in UserDataExtension at `(char*)this+sizeof(Block)'
    char* data;
        
    Block(char* data_in, uint32_t data_size)
        : nshared(1)
        , flags(0)
        , abi_check(0)
        , size(0)
        , cap(data_size)
        , u({NULL})
        , data(data_in) {
        iobuf::inc_g_nblock();
        iobuf::inc_g_blockmem();
        if (is_samplable()) {
            SubmitIOBufSample(this, 1);
        }
    }

    Block(char* data_in, uint32_t data_size, UserDataDeleter deleter)
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

    // Undefined behavior when (flags & IOBUF_BLOCK_FLAGS_USER_DATA) is 0.
    UserDataExtension* get_user_data_extension() {
        char* p = (char*)this;
        return (UserDataExtension*)(p + sizeof(Block));
    }

    inline void check_abi() {
#ifndef NDEBUG
    if (abi_check != 0) {
        LOG(FATAL) << "Your program seems to wrongly contain two "
            "ABI-incompatible implementations of IOBuf";
    }
#endif
}

    void inc_ref() {
        check_abi();
        nshared.fetch_add(1, butil::memory_order_relaxed);
        if (sampled()) {
            SubmitIOBufSample(this, 1);
        }
    }
        
    void dec_ref() {
        check_abi();
        if (sampled()) {
            SubmitIOBufSample(this, -1);
        }
        if (nshared.fetch_sub(1, butil::memory_order_release) == 1) {
            butil::atomic_thread_fence(butil::memory_order_acquire);
            if (!is_user_data()) {
                iobuf::dec_g_nblock();
                iobuf::dec_g_blockmem();
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

    int ref_count() const {
        return nshared.load(butil::memory_order_relaxed);
    }

    bool full() const { return size >= cap; }
    size_t left_space() const { return cap - size; }

private:
    bool is_samplable() {
        if (IsIOBufProfilerSamplable()) {
            flags |= IOBUF_BLOCK_FLAGS_SAMPLED;
            return true;
        }
        return false;
    }

    bool sampled() const {
        return flags & IOBUF_BLOCK_FLAGS_SAMPLED;
    }

    bool is_user_data() const {
        return flags & IOBUF_BLOCK_FLAGS_USER_DATA;
    }
};

namespace iobuf {
struct TLSData {
    // Head of the TLS block chain.
    IOBuf::Block* block_head;
    
    // Number of TLS blocks
    int num_blocks;
    
    // True if the remote_tls_block_chain is registered to the thread.
    bool registered;
};

// Max number of blocks in each TLS. This is a soft limit namely
// release_tls_block_chain() may exceed this limit sometimes.
const int MAX_BLOCKS_PER_THREAD = 8;

inline int max_blocks_per_thread() {
    // If IOBufProfiler is enabled, do not cache blocks in TLS.
    return IsIOBufProfilerEnabled() ? 0 : MAX_BLOCKS_PER_THREAD;
}

TLSData* get_g_tls_data();
void remove_tls_block_chain();

IOBuf::Block* acquire_tls_block();

// Return one block to TLS.
inline void release_tls_block(IOBuf::Block* b) {
    if (!b) {
        return;
    }
    TLSData *tls_data = get_g_tls_data();
    if (b->full()) {
        b->dec_ref();
    } else if (tls_data->num_blocks >= max_blocks_per_thread()) {
        b->dec_ref();
        // g_num_hit_tls_threshold.fetch_add(1, butil::memory_order_relaxed);
        inc_g_num_hit_tls_threshold();
    } else {
        b->u.portal_next = tls_data->block_head;
        tls_data->block_head = b;
        ++tls_data->num_blocks;
        if (!tls_data->registered) {
            tls_data->registered = true;
            butil::thread_atexit(remove_tls_block_chain);
        }
    }
}

inline IOBuf::Block* create_block(const size_t block_size) {
    if (block_size > 0xFFFFFFFFULL) {
        LOG(FATAL) << "block_size=" << block_size << " is too large";
        return NULL;
    }
    char* mem = (char*)iobuf::blockmem_allocate(block_size);
    if (mem == NULL) {
        return NULL;
    }
    return new (mem) IOBuf::Block(mem + sizeof(IOBuf::Block),
                                  block_size - sizeof(IOBuf::Block));
}

inline IOBuf::Block* create_block() {
    return create_block(IOBuf::DEFAULT_BLOCK_SIZE);
}

void* cp(void *__restrict dest, const void *__restrict src, size_t n);

};  // namespace iobuf;

}  // namespace butil

#endif  // BUTIL_IOBUF_INL_H