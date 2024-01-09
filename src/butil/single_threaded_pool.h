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

// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BUTIL_SINGLE_THREADED_POOL_H
#define BUTIL_SINGLE_THREADED_POOL_H

#include <stdlib.h>   // malloc & free

namespace butil {

// A single-threaded pool for very efficient allocations of same-sized items.
// Example:
//   SingleThreadedPool<16, 512> pool;
//   void* mem = pool.get();
//   pool.back(mem);

class PtAllocator {
public:
    void* Alloc(size_t n) { return malloc(n); }
    void Free(void* p) { free(p); }
};

template <size_t ITEM_SIZE_IN,   // size of an item
          size_t BLOCK_SIZE_IN,  // suggested size of a block
          size_t MIN_NITEM = 1,
          typename Allocator = PtAllocator>  // minimum number of items in one block
class SingleThreadedPool {
public:
    // Note: this is a union. The next pointer is set iff when spaces is free,
    // ok to be overlapped.
    union Node {
        Node* next;
        char spaces[ITEM_SIZE_IN];
    };
    struct Block {
        static const size_t INUSE_SIZE =
            BLOCK_SIZE_IN - sizeof(void*) - sizeof(size_t);
        static const size_t NITEM = (sizeof(Node) <= INUSE_SIZE ?
                                     (INUSE_SIZE / sizeof(Node)) : MIN_NITEM);
        size_t nalloc;
        Block* next;
        Node nodes[NITEM];
    };
    static const size_t BLOCK_SIZE = sizeof(Block);
    static const size_t NITEM = Block::NITEM;
    static const size_t ITEM_SIZE = ITEM_SIZE_IN;
    
    SingleThreadedPool(const Allocator& alloc = Allocator()) 
        : _free_nodes(NULL), _blocks(NULL), _allocator(alloc) {}
    ~SingleThreadedPool() { reset(); }

    void swap(SingleThreadedPool & other) {
        std::swap(_free_nodes, other._free_nodes);
        std::swap(_blocks, other._blocks);
        std::swap(_allocator, other._allocator);
    }

    // Get space of an item. The space is as long as ITEM_SIZE.
    // Returns NULL on out of memory
    void* get() {
        if (_free_nodes) {
            void* spaces = _free_nodes->spaces;
            _free_nodes = _free_nodes->next;
            return spaces;
        }
        if (_blocks == NULL || _blocks->nalloc >= Block::NITEM) {
            Block* new_block = (Block*)_allocator.Alloc(sizeof(Block));
            if (new_block == NULL) {
                return NULL;
            }
            new_block->nalloc = 0;
            new_block->next = _blocks;
            _blocks = new_block;
        }
        return _blocks->nodes[_blocks->nalloc++].spaces;
    }
    
    // Return a space allocated by get() before.
    // Do nothing for NULL.
    void back(void* p) {
        if (NULL != p) {
            Node* node = (Node*)((char*)p - offsetof(Node, spaces));
            node->next = _free_nodes;
            _free_nodes = node;
        }
    }

    // Remove all allocated spaces. Spaces that are not back()-ed yet become
    // invalid as well.
    void reset() {
        _free_nodes = NULL;
        while (_blocks) {
            Block* next = _blocks->next;
            _allocator.Free(_blocks);
            _blocks = next;
        }
    }

    // Count number of allocated/free/actively-used items.
    // Notice that these functions walk through all free nodes or blocks and
    // are not O(1).
    size_t count_allocated() const {
        size_t n = 0;
        for (Block* p = _blocks; p; p = p->next) {
            n += p->nalloc;
        }
        return n;
    }
    size_t count_free() const {
        size_t n = 0;
        for (Node* p = _free_nodes; p; p = p->next, ++n) {}
        return n;
    }
    size_t count_active() const {
        return count_allocated() - count_free();
    }

    Allocator& get_allocator() { return _allocator; }

private:
    // You should not copy a pool.
    SingleThreadedPool(const SingleThreadedPool&);
    void operator=(const SingleThreadedPool&);

    Node* _free_nodes;
    Block* _blocks;
    Allocator _allocator;
};

}  // namespace butil

#endif  // BUTIL_SINGLE_THREADED_POOL_H
