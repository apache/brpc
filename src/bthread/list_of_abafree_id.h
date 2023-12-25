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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Mon Jun 20 11:57:23 CST 2016

#ifndef BTHREAD_LIST_OF_ABAFREE_ID_H
#define BTHREAD_LIST_OF_ABAFREE_ID_H

#include <deque>
#include <vector>

#include "butil/macros.h"

namespace bthread {

// A container for storing identifiers that may be invalidated.

// [Basic Idea]
// identifiers are remembered for error notifications. While insertions are 
// easy, removals are hard to be done in O(1) time. More importantly, 
// insertions are often done in one thread, while removals come from many
// threads simultaneously. Think about the usage in brpc::Socket, most 
// bthread_id_t are inserted by one thread (the thread calling non-contended
// Write or the KeepWrite thread), but removals are from many threads 
// processing responses simultaneously.

// [The approach]
// Don't remove old identifiers eagerly, replace them when new ones are inserted.

// IdTraits MUST have {
//   // #identifiers in each block
//   static const size_t BLOCK_SIZE = 63;
//
//   // Max #entries. Often has close relationship with concurrency, 65536
//   // is "huge" for most apps.
//   static const size_t MAX_ENTRIES = 65536;
//   // Initial GC length, when the number of blocks reaches this length, 
//   // start to initiate list GC operation. It will release useless blocks
//   static const size_t INIT_GC_SIZE = 4096;
//
//   // Initial value of id. Id with the value is treated as invalid.
//   static const Id ID_INIT = ...;
//
//   // Returns true if the id is valid. The "validness" must be permanent or
//   // stable for a very long period (to make the id ABA-free).
//   static bool exists(Id id);
// }

// This container is NOT thread-safe right now, and shouldn't be
// an issue in current usages throughout brpc.
template <typename Id, typename IdTraits> 
class ListOfABAFreeId {
public:
    ListOfABAFreeId();
    ~ListOfABAFreeId();

    // Add an identifier into the list.
    int add(Id id);

    // Apply fn(id) to all identifiers.
    template <typename Fn>
    void apply(const Fn& fn);

    // Put #entries of each level into `counts'
    // Returns #levels.
    size_t get_sizes(size_t* counts, size_t n);

private:
    DISALLOW_COPY_AND_ASSIGN(ListOfABAFreeId);
    struct IdBlock {
        Id ids[IdTraits::BLOCK_SIZE];
        IdBlock* next;
    };
    void forward_index();

    struct TempIdBlock {
        IdBlock* block;
        uint32_t index;
        uint32_t nblock;
    };

    int gc();
    int add_to_temp_list(TempIdBlock* temp_list, Id id);
    template <typename Fn>
    int for_each(const Fn& fn);
    void free_list(IdBlock* block);

    IdBlock* _cur_block;
    uint32_t _cur_index;
    uint32_t _nblock;
    IdBlock _head_block;
    uint32_t _next_gc_size;
};

// [impl.]

template <typename Id, typename IdTraits> 
ListOfABAFreeId<Id, IdTraits>::ListOfABAFreeId()
        : _cur_block(&_head_block), _cur_index(0), _nblock(1), _next_gc_size(IdTraits::INIT_GC_SIZE) {
    for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
        _head_block.ids[i] = IdTraits::ID_INIT;
    }
    _head_block.next = NULL;
}

template <typename Id, typename IdTraits> 
ListOfABAFreeId<Id, IdTraits>::~ListOfABAFreeId() {
    _cur_block = NULL;
    _cur_index = 0;
    _nblock = 0;
    for (IdBlock* p = _head_block.next; p != NULL;) {
        IdBlock* saved_next = p->next;
        delete p;
        p = saved_next;
    }
    _head_block.next = NULL;
}

template <typename Id, typename IdTraits> 
inline void ListOfABAFreeId<Id, IdTraits>::forward_index() {
    if (++_cur_index >= IdTraits::BLOCK_SIZE) {
        _cur_index = 0;
        if (_cur_block->next) {
            _cur_block = _cur_block->next;
        } else {
            _cur_block = &_head_block;
        }
    }
}

template <typename Id, typename IdTraits> 
int ListOfABAFreeId<Id, IdTraits>::add(Id id) {
    // Scan for at most 4 positions, if any of them is empty, use the position.
    Id* saved_pos[4];
    for (size_t i = 0; i < arraysize(saved_pos); ++i) {
        Id* const pos = _cur_block->ids + _cur_index;
        forward_index();
        // The position is not used.
        if (*pos == IdTraits::ID_INIT || !IdTraits::exists(*pos)) {
            *pos = id;
            return 0;
        }
        saved_pos[i] = pos;
    }
    // If we don't expect a GC to occur in abalist, then an error is reported and EAGAIN is returned.
    if (_nblock * IdTraits::BLOCK_SIZE > IdTraits::MAX_ENTRIES) {
        return EAGAIN;
    }
    // If the number of blocks exceeds the minimum GC length, start the GC operation
    if (_nblock * IdTraits::BLOCK_SIZE > _next_gc_size) {
        uint32_t before_gc_blocks = _nblock;
        int rc = gc();
        // To avoid frequent GC operations, we only let the GC be effective enough to continue the GC. 
        // otherwise we let the next GC occur length * 2.
        // 
        // Condition for a GC to be sufficiently efficient: the number of blocks 
        // retained after the GC is 1/4 of the previous one.
        if ((before_gc_blocks - _nblock) * IdTraits::BLOCK_SIZE < (_next_gc_size - (_next_gc_size >> 2))) {
            _next_gc_size <<= 1;
            // We want to make sure that GC must occur before MAX_ENTRIES.
            static_assert(IdTraits::MAX_ENTRIES > IdTraits::BLOCK_SIZE * 2, "MAX_ENTRIES should be greater than 2 * IdTraits::BLOCK_SIZE");
            if (_next_gc_size >= IdTraits::MAX_ENTRIES) {
                _next_gc_size = IdTraits::MAX_ENTRIES - IdTraits::BLOCK_SIZE * 2;
            }
        }

        return rc;
    }
    // The list is considered to be "crowded", add a new block and scatter
    // the conflict identifiers by inserting an empty entry after each of
    // them, so that even if the identifiers are still valid when we walk
    // through the area again, we can find an empty entry.

    // The new block is inserted as if it's inserted between xxxx and yyyy,
    // where xxxx are the 4 conflict identifiers.
    //  [..xxxxyyyy] -> [..........]
    //    block A        block B
    //
    //  [..xxxx....] -> [......yyyy] -> [..........]
    //    block A        new block      block B
    IdBlock* new_block = new (std::nothrow) IdBlock;
    if (NULL == new_block) {
        return ENOMEM;
    }
    ++_nblock;
    for (size_t i = 0; i < _cur_index; ++i) {
        new_block->ids[i] = IdTraits::ID_INIT;
    }
    for (size_t i = _cur_index; i < IdTraits::BLOCK_SIZE; ++i) {
        new_block->ids[i] = _cur_block->ids[i];
        _cur_block->ids[i] = IdTraits::ID_INIT;
    }
    new_block->next = _cur_block->next;
    _cur_block->next = new_block;
    // Scatter the conflict identifiers.
    //  [..xxxx....] -> [......yyyy] -> [..........]
    //    block A        new block      block B
    //
    //  [..x.x.x.x.] -> [......yyyy] -> [..........]
    //    block A        new block      block B
    _cur_block->ids[_cur_index] = *saved_pos[2];
    *saved_pos[2] = *saved_pos[1];
    *saved_pos[1] = IdTraits::ID_INIT;
    forward_index();
    forward_index();
    _cur_block->ids[_cur_index] = *saved_pos[3];
    *saved_pos[3] = IdTraits::ID_INIT;
    forward_index();
    _cur_block->ids[_cur_index] = id;
    forward_index();
    return 0;
}

template <typename Id, typename IdTraits>
int ListOfABAFreeId<Id, IdTraits>::gc() {
    IdBlock* new_block = new (std::nothrow) IdBlock;
    if (NULL == new_block) {
        return ENOMEM;
    }
    // reset head block
    for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
        new_block->ids[i] = IdTraits::ID_INIT;
    }
    new_block->next = NULL;

    TempIdBlock tmp_id_block;
    tmp_id_block.block = new_block;
    tmp_id_block.nblock = 1;
    tmp_id_block.index = 0;

    // Add each element of the old list to the new list
    int rc = for_each([&](Id id) {
        int rc;
        rc = add_to_temp_list(&tmp_id_block, id);
        if (rc != 0) {
            return rc;
        }
        rc = add_to_temp_list(&tmp_id_block, IdTraits::ID_INIT);
        if (rc != 0) {
            return rc;
        }
        return 0;
    });

    if (rc != 0) {
        free_list(new_block);
        return rc;
    }

    // reset head block
    for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
        _head_block.ids[i] = IdTraits::ID_INIT;
    }

    free_list(_head_block.next);
    _cur_block = tmp_id_block.block;
    _cur_index = tmp_id_block.index;
    // nblock and head_block
    _nblock = tmp_id_block.nblock + 1;
    _head_block.next = new_block;

    return 0;
}

template <typename Id, typename IdTraits>
int ListOfABAFreeId<Id, IdTraits>::add_to_temp_list(TempIdBlock* block_list, Id id) {
    block_list->block->ids[block_list->index++] = id;
    // add new list
    if (block_list->index == IdTraits::BLOCK_SIZE) {
        block_list->index = 0;
        block_list->nblock++;
        block_list->block->next = new (std::nothrow) IdBlock;
        if (NULL == block_list->block->next) {
            return ENOMEM;
        }
        block_list->block = block_list->block->next;
        for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
            block_list->block->ids[i] = IdTraits::ID_INIT;
        }
        block_list->block->next = NULL;
    }
    return 0;
}

template <typename Id, typename IdTraits>
template <typename Fn>
int ListOfABAFreeId<Id, IdTraits>::for_each(const Fn& fn) {
    for (IdBlock* p = &_head_block; p != NULL; p = p->next) {
        for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
            if (p->ids[i] != IdTraits::ID_INIT && IdTraits::exists(p->ids[i])) {
                int rc = fn(p->ids[i]);
                if (rc != 0) {
                    return rc;
                }
            }
        }
    }
    return 0;
}

template <typename Id, typename IdTraits>
template <typename Fn>
void ListOfABAFreeId<Id, IdTraits>::apply(const Fn& fn) {
    for (IdBlock* p = &_head_block; p != NULL; p = p->next) {
        for (size_t i = 0; i < IdTraits::BLOCK_SIZE; ++i) {
            if (p->ids[i] != IdTraits::ID_INIT && IdTraits::exists(p->ids[i])) {
                fn(p->ids[i]);
            }
        }
    }
}

template <typename Id, typename IdTraits>
void ListOfABAFreeId<Id, IdTraits>::free_list(IdBlock* p) {
    for (; p != NULL;) {
        IdBlock* saved_next = p->next;
        delete p;
        p = saved_next;
    }
}

template <typename Id, typename IdTraits>
size_t ListOfABAFreeId<Id, IdTraits>::get_sizes(size_t* cnts, size_t n) {
    if (n == 0) {
        return 0;
    }
    // Current impl. only has one level.
    cnts[0] = _nblock * IdTraits::BLOCK_SIZE;
    return 1;
}

} // namespace bthread

#endif // BTHREAD_LIST_OF_ABAFREE_ID_H
