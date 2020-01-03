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

// Date: Fri Jun  5 18:25:40 CST 2015

// Do small memory allocations on continuous blocks.

#ifndef BUTIL_ARENA_H
#define BUTIL_ARENA_H

#include <stdint.h>
#include "butil/macros.h"

namespace butil {

struct ArenaOptions {
    size_t initial_block_size;
    size_t max_block_size;

    // Constructed with default options.
    ArenaOptions();
};

// Just a proof-of-concept, will be refactored in future CI.
class Arena {
public:
    explicit Arena(const ArenaOptions& options = ArenaOptions());
    ~Arena();
    void swap(Arena&);
    void* allocate(size_t n);
    void* allocate_aligned(size_t n);  // not implemented.
    void clear();

private:
    DISALLOW_COPY_AND_ASSIGN(Arena);

    struct Block {
        uint32_t left_space() const { return size - alloc_size; }
        
        Block* next;
        uint32_t alloc_size;
        uint32_t size;
        char data[0];
    };

    void* allocate_in_other_blocks(size_t n);
    void* allocate_new_block(size_t n);
    Block* pop_block(Block* & head) {
        Block* saved_head = head;
        head = head->next;
        return saved_head;
    }
    
    Block* _cur_block;
    Block* _isolated_blocks;
    size_t _block_size;
    ArenaOptions _options;
};

inline void* Arena::allocate(size_t n) {
    if (_cur_block != NULL && _cur_block->left_space() >= n) {
        void* ret = _cur_block->data + _cur_block->alloc_size;
        _cur_block->alloc_size += n;
        return ret;
    }
    return allocate_in_other_blocks(n);
}

}  // namespace butil

#endif  // BUTIL_ARENA_H
