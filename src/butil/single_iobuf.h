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

#ifndef BUTIL_SINGLE_IOBUF_H
#define BUTIL_SINGLE_IOBUF_H

#include "butil/iobuf.h"
#include "butil/iobuf_inl.h"

namespace butil {

// SingleIOBuf is a lightweight buffer that manages a single IOBuf::Block.
// It always ensures that the underlying memory is contiguous and 
// avoids unnecessary memory copies.
// It is primarily used to efficiently serialize and deserialize 
// RPC requests in flatbuffers.
class SingleIOBuf {
public:
    SingleIOBuf();
    ~SingleIOBuf();
    SingleIOBuf(const IOBuf::BlockRef& ref);
    SingleIOBuf(const SingleIOBuf& other);
    SingleIOBuf& operator=(const SingleIOBuf& rhs);
    void swap(SingleIOBuf& other);

    // Allocates a contiguous memory region of the specified size.
    // Returns a pointer to the start of allocated space within the block.
    void* allocate(uint32_t size);
    // Deallocates the block if the given pointer matches its starting address.
    void deallocate(void* p);

    // Reallocates the current buffer to a larger size.
    // in_use_back indicates the memory used by message data.
    // in_use_front indicates the memory used by metadata.
    void* reallocate_downward(uint32_t new_size,
                            uint32_t in_use_back,
                            uint32_t in_use_front);

    // Returns a pointer to the beginning of the current buffer data.
    const void* get_begin() const;
    
    // Get the length of the SingleIOBuf.
    uint32_t get_length() const;
    
    // Reset the SingleIOBuf by release the block and clear the BlockRef.
    void reset();

    // Assigns data from the given IOBuf to this SingleIOBuf.
    // If the source contains multiple BlockRef segments, 
    // they will be concatenated into a single contiguous block.
    bool assign(const IOBuf& buf, uint32_t msg_size);
    
    // Appends the current block of the SingleIOBuf to the given IOBuf.
    void append_to(IOBuf* buf) const;

    const IOBuf::BlockRef& get_cur_ref() const { return _cur_ref; }
    void* get_cur_block() { return (void*)_cur_ref.block; }

    // Returns the number of underlying blocks in the SingleIOBuf,
    // which is either 1 if a block exists or 0 if none.
    size_t backing_block_num() const { return _cur_ref.block != NULL ? 1 : 0; }
    
    // Assigns user date to the SingleIOBuf,
    // updates _cur_ref to point to the block storing the data.
    int assign_user_data(void* data, size_t size, std::function<void(void*)> deleter);
    
    // Increments the reference count of the specified target block.
    static void target_block_inc_ref(void* b);  
    // Decrements the reference count of the specified target block.
    static void target_block_dec_ref(void* b);

protected:
    // Copy from the old buffer to the corresponding positions in the new block.
    void memcpy_downward(void* old_p, uint32_t old_size,
                        void* new_p, uint32_t new_size,
                        uint32_t in_use_back, uint32_t in_use_front);

    // Allocates and returns a memory block large enough to hold the specified data size, 
    // reusing an existing block when possible or creating a new one if necessary.
    IOBuf::Block* alloc_block_by_size(uint32_t data_size);
    
private:
    // Current block the SingleIOBuf used to allocate memory.
    IOBuf::Block* _cur_block;
    // Current block total size, include sizeof(IOBuf::Block).
    uint32_t _block_size;
    // Point to the block storing the data.
    IOBuf::BlockRef _cur_ref;
};

}  // namespace butil

#endif