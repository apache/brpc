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

// bthread - A M:N threading library to make applications more concurrent.

// Date: 2015/10/27 17:39:48

#ifndef  BTHREAD_EXECUTION_QUEUE_INL_H
#define  BTHREAD_EXECUTION_QUEUE_INL_H

#include "butil/atomicops.h"             // butil::atomic
#include "butil/macros.h"                // BAIDU_CACHELINE_ALIGNMENT
#include "butil/memory/scoped_ptr.h"     // butil::scoped_ptr
#include "butil/logging.h"               // LOG
#include "butil/time.h"                  // butil::cpuwide_time_ns
#include "bvar/bvar.h"                  // bvar::Adder
#include "bthread/butex.h"              // butex_construct

namespace bthread {

template <typename T>
struct ExecutionQueueId {
    uint64_t value;
};

enum TaskStatus {
    UNEXECUTED = 0,
    EXECUTING = 1,
    EXECUTED = 2
};

struct TaskNode;
class ExecutionQueueBase;
typedef void (*clear_task_mem)(TaskNode*);

struct BAIDU_CACHELINE_ALIGNMENT TaskNode {
    TaskNode()
        : version(0)
        , status(UNEXECUTED)
        , stop_task(false)
        , iterated(false) 
        , high_priority(false)
        , in_place(false) 
        , next(UNCONNECTED)
        , q(NULL)
    {}
    ~TaskNode() {}
    int cancel(int64_t expected_version) {
        BAIDU_SCOPED_LOCK(mutex);
        if (version != expected_version) {
            return -1;
        }
        if (status == UNEXECUTED) {
            status = EXECUTED;
            return 0;
        }
        return status == EXECUTED ? -1 : 1;
    }
    void set_executed() {
        BAIDU_SCOPED_LOCK(mutex);
        status = EXECUTED;
    }
    bool peek_to_execute() {
        BAIDU_SCOPED_LOCK(mutex);
        if (status == UNEXECUTED) {
            status = EXECUTING;
            return true;
        }
        return false;
    }
    butil::Mutex mutex;  // to guard version and status
    int64_t version;
    uint8_t status;
    bool stop_task;
    bool iterated;
    bool high_priority;
    bool in_place;
    TaskNode* next;
    ExecutionQueueBase* q;
    union {
        char static_task_mem[56];  // Make sizeof TaskNode exactly 128 bytes
        char* dynamic_task_mem;
    };

    void clear_before_return(clear_task_mem clear_func) {
        if (!stop_task) {
            clear_func(this);
            CHECK(iterated);
        }
        q = NULL;
        std::unique_lock<butil::Mutex> lck(mutex);
        ++version;
        const int saved_status = status;
        status = UNEXECUTED;
        lck.unlock();
        CHECK_NE(saved_status, UNEXECUTED);
        LOG_IF(WARNING, saved_status == EXECUTING) 
                << "Return a executing node, did you return before "
                   "iterator reached the end?";
    }

    static TaskNode* const UNCONNECTED;
};

// Specialize TaskNodeAllocator for types with different sizes
template <size_t size, bool small_object> struct TaskAllocatorBase {
};

template <size_t size>
struct TaskAllocatorBase<size, true> {
    inline static void* allocate(TaskNode* node)
    { return node->static_task_mem; }
    inline static void* get_allocated_mem(TaskNode* node)
    { return node->static_task_mem; }
    inline static void deallocate(TaskNode*) {}
};

template<size_t size>
struct TaskAllocatorBase<size, false> {
    inline static void* allocate(TaskNode* node) {
        node->dynamic_task_mem = (char*)malloc(size);
        return node->dynamic_task_mem;
    }

    inline static void* get_allocated_mem(TaskNode* node)
    { return node->dynamic_task_mem; }

    inline static void deallocate(TaskNode* node) {
        free(node->dynamic_task_mem);
    }
};

template <typename T>
struct TaskAllocator : public TaskAllocatorBase<
               sizeof(T), sizeof(T) <= sizeof(TaskNode().static_task_mem)>
{};

class TaskIteratorBase;

class BAIDU_CACHELINE_ALIGNMENT ExecutionQueueBase {
DISALLOW_COPY_AND_ASSIGN(ExecutionQueueBase);
struct Forbidden {};
friend class TaskIteratorBase;
    struct Dereferencer {
        void operator()(ExecutionQueueBase* queue) {
            if (queue != NULL) {
                queue->dereference();
            }
        }
    };
public:
    // User cannot create ExecutionQueue fron construct
    ExecutionQueueBase(Forbidden)
        : _head(NULL)
        , _versioned_ref(0)  // join() depends on even version
        , _high_priority_tasks(0)
    {
        _join_butex = butex_create_checked<butil::atomic<int> >();
        _join_butex->store(0, butil::memory_order_relaxed);
    }

    ~ExecutionQueueBase() {
        butex_destroy(_join_butex);
    }

    bool stopped() const { return _stopped.load(butil::memory_order_acquire); }
    int stop();
    static int join(uint64_t id);
protected:
    typedef int (*execute_func_t)(void*, void*, TaskIteratorBase&);
    typedef scoped_ptr<ExecutionQueueBase, Dereferencer>  scoped_ptr_t;
    int dereference();
    static int create(uint64_t* id, const ExecutionQueueOptions* options,
                      execute_func_t execute_func,
                      clear_task_mem clear_func,
                      void* meta, void* type_specific_function);
    static scoped_ptr_t address(uint64_t id) WARN_UNUSED_RESULT;
    void start_execute(TaskNode* node);
    TaskNode* allocate_node();
    void return_task_node(TaskNode* node);

private:

    bool _more_tasks(TaskNode* old_head, TaskNode** new_tail,
                     bool has_uniterated);
    void _release_additional_reference() {
        dereference();
    }
    void _on_recycle();
    int _execute(TaskNode* head, bool high_priority, int* niterated);
    static void* _execute_tasks(void* arg);

    static inline uint32_t _version_of_id(uint64_t id) WARN_UNUSED_RESULT {
        return (uint32_t)(id >> 32);
    }

    static inline uint32_t _version_of_vref(int64_t vref) WARN_UNUSED_RESULT {
        return (uint32_t)(vref >> 32);
    }

    static inline uint32_t _ref_of_vref(int64_t vref) WARN_UNUSED_RESULT {
        return (int32_t)(vref & 0xFFFFFFFFul);
    }

    static inline int64_t _make_vref(uint32_t version, int32_t ref) {
        // 1: Intended conversion to uint32_t, nref=-1 is 00000000FFFFFFFF
        return (((uint64_t)version) << 32) | (uint32_t/*1*/)ref;
    }

    // Don't change the order of _head, _versioned_ref and _stopped unless you 
    // see improvement of performance in test
    butil::atomic<TaskNode*> BAIDU_CACHELINE_ALIGNMENT _head;
    butil::atomic<uint64_t> BAIDU_CACHELINE_ALIGNMENT _versioned_ref;
    butil::atomic<bool> BAIDU_CACHELINE_ALIGNMENT _stopped;
    butil::atomic<int64_t> _high_priority_tasks;
    uint64_t _this_id;
    void* _meta;
    void* _type_specific_function;
    execute_func_t _execute_func;
    clear_task_mem _clear_func;
    ExecutionQueueOptions _options;
    butil::atomic<int>* _join_butex;
};

template <typename T>
class ExecutionQueue : public ExecutionQueueBase {
struct Forbidden {};
friend class TaskIterator<T>;
    typedef ExecutionQueueBase                                  Base;
    ExecutionQueue();
public:
    typedef ExecutionQueue<T>                                   self_type;
    struct Dereferencer {
        void operator()(self_type* queue) {
            if (queue != NULL) {
                queue->dereference();
            }
        }
    };
    typedef scoped_ptr<self_type, Dereferencer>                 scoped_ptr_t;
    typedef bthread::ExecutionQueueId<T>                        id_t;
    typedef TaskIterator<T>                                     iterator;
    typedef int (*execute_func_t)(void*, iterator&);
    typedef TaskAllocator<T>                                    allocator;
    BAIDU_CASSERT(sizeof(execute_func_t) == sizeof(void*),
                  sizeof_function_must_be_equal_to_sizeof_voidptr);

    static void clear_task_mem(TaskNode* node) {
        T* const task = (T*)allocator::get_allocated_mem(node);
        task->~T();
        allocator::deallocate(node);
    }

    static int execute_task(void* meta, void* specific_function,
                            TaskIteratorBase& it) {
        execute_func_t f = (execute_func_t)specific_function;
        return f(meta, static_cast<iterator&>(it));
    }

    inline static int create(id_t* id, const ExecutionQueueOptions* options,
                             execute_func_t execute_func, void* meta) {
        return Base::create(&id->value, options, execute_task, 
                            clear_task_mem, meta, (void*)execute_func);
    }

    inline static scoped_ptr_t address(id_t id) WARN_UNUSED_RESULT {
        Base::scoped_ptr_t ptr = Base::address(id.value);
        Base* b = ptr.release();
        scoped_ptr_t ret((self_type*)b);
        return ret.Pass();
    }

    int execute(typename butil::add_const_reference<T>::type task) {
        return execute(task, NULL, NULL);
    }

    int execute(typename butil::add_const_reference<T>::type task,
                const TaskOptions* options, TaskHandle* handle) {
        if (stopped()) {
            return EINVAL;
        }
        TaskNode* node = allocate_node();
        if (BAIDU_UNLIKELY(node == NULL)) {
            return ENOMEM;
        }
        void* const mem = allocator::allocate(node);
        if (BAIDU_UNLIKELY(!mem)) {
            return_task_node(node);
            return ENOMEM;
        }
        new (mem) T(task);
        node->stop_task = false;
        TaskOptions opt;
        if (options) {
            opt = *options;
        }
        node->high_priority = opt.high_priority;
        node->in_place = opt.in_place_if_possible;
        if (handle) {
            handle->node = node;
            handle->version = node->version;
        }
        start_execute(node);
        return 0;
    }
};

inline ExecutionQueueOptions::ExecutionQueueOptions()
    : bthread_attr(BTHREAD_ATTR_NORMAL), executor(NULL)
{}

template <typename T>
inline int execution_queue_start(
        ExecutionQueueId<T>* id, 
        const ExecutionQueueOptions* options,
        int (*execute)(void* meta, TaskIterator<T>&),
        void* meta) {
   return ExecutionQueue<T>::create(id, options, execute, meta);
}

template <typename T>
typename ExecutionQueue<T>::scoped_ptr_t 
execution_queue_address(ExecutionQueueId<T> id) {
    return ExecutionQueue<T>::address(id);
}

template <typename T>
inline int execution_queue_execute(ExecutionQueueId<T> id, 
                       typename butil::add_const_reference<T>::type task) {
    return execution_queue_execute(id, task, NULL);
}

template <typename T>
inline int execution_queue_execute(ExecutionQueueId<T> id, 
                       typename butil::add_const_reference<T>::type task,
                       const TaskOptions* options) {
    return execution_queue_execute(id, task, options, NULL);
}

template <typename T>
inline int execution_queue_execute(ExecutionQueueId<T> id, 
                       typename butil::add_const_reference<T>::type task,
                       const TaskOptions* options,
                       TaskHandle* handle) {
    typename ExecutionQueue<T>::scoped_ptr_t 
        ptr = ExecutionQueue<T>::address(id);
    if (ptr != NULL) {
        return ptr->execute(task, options, handle);
    } else {
        return EINVAL;
    }
}

template <typename T>
inline int execution_queue_stop(ExecutionQueueId<T> id) {
    typename ExecutionQueue<T>::scoped_ptr_t 
        ptr = ExecutionQueue<T>::address(id);
    if (ptr != NULL) {
        return ptr->stop();
    } else {
        return EINVAL;
    }
}

template <typename T>
inline int execution_queue_join(ExecutionQueueId<T> id) {
    return ExecutionQueue<T>::join(id.value);
}

inline TaskOptions::TaskOptions()
    : high_priority(false)
    , in_place_if_possible(false)
{}

inline TaskOptions::TaskOptions(bool high_priority, bool in_place_if_possible)
    : high_priority(high_priority)
    , in_place_if_possible(in_place_if_possible)
{}

//--------------------- TaskIterator ------------------------

inline TaskIteratorBase::operator bool() const {
    return !_is_stopped && !_should_break && _cur_node != NULL 
           && !_cur_node->stop_task;
}

template <typename T>
inline typename TaskIterator<T>::reference
TaskIterator<T>::operator*() const {
    T* const ptr = (T* const)TaskAllocator<T>::get_allocated_mem(cur_node());
    return *ptr;
}

template <typename T>
TaskIterator<T>& TaskIterator<T>::operator++() {
    TaskIteratorBase::operator++();
    return *this;
}

template <typename T>
void TaskIterator<T>::operator++(int) {
    operator++();
}

inline TaskHandle::TaskHandle()
    : node(NULL)
    , version(0)
{}

inline int execution_queue_cancel(const TaskHandle& h) {
    if (h.node == NULL) {
        return -1;
    }
    return h.node->cancel(h.version);
}

// ---------------------ExecutionQueueBase--------------------
inline bool ExecutionQueueBase::_more_tasks(
        TaskNode* old_head, TaskNode** new_tail, 
        bool has_uniterated) {

    CHECK(old_head->next == NULL);
    // Try to set _head to NULL to mark that the execute is done.
    TaskNode* new_head = old_head;
    TaskNode* desired = NULL;
    bool return_when_no_more = false;
    if (has_uniterated) {
        desired = old_head;
        return_when_no_more = true;
    }
    if (_head.compare_exchange_strong(
                new_head, desired, butil::memory_order_acquire)) {
        // No one added new tasks.
        return return_when_no_more;
    }
    CHECK_NE(new_head, old_head);
    // Above acquire fence pairs release fence of exchange in Write() to make
    // sure that we see all fields of requests set.

    // Someone added new requests.
    // Reverse the list until old_head.
    TaskNode* tail = NULL;
    if (new_tail) {
        *new_tail = new_head;
    }
    TaskNode* p = new_head;
    do {
        while (p->next == TaskNode::UNCONNECTED) {
            // TODO(gejun): elaborate this
            sched_yield();
        }
        TaskNode* const saved_next = p->next;
        p->next = tail;
        tail = p;
        p = saved_next;
        CHECK(p != NULL);
    } while (p != old_head);

    // Link old list with new list.
    old_head->next = tail;
    return true;
}

inline int ExecutionQueueBase::dereference() {
    const uint64_t vref = _versioned_ref.fetch_sub(
            1, butil::memory_order_release);
    const int32_t nref = _ref_of_vref(vref);
    // We need make the fast path as fast as possible, don't put any extra
    // code before this point
    if (nref > 1) {
        return 0;
    }
    const uint64_t id = _this_id;
    if (__builtin_expect(nref == 1, 1)) {
        const uint32_t ver = _version_of_vref(vref);
        const uint32_t id_ver = _version_of_id(id);
        // Besides first successful stop() adds 1 to version, one of
        // those dereferencing nref from 1->0 adds another 1 to version.
        // Notice "one of those": The wait-free address() may make ref of a
        // version-unmatched slot change from 1 to 0 for mutiple times, we
        // have to use version as a guard variable to prevent returning the
        // executor to pool more than once.
        if (__builtin_expect(ver == id_ver || ver == id_ver + 1, 1)) {
            // sees nref:1->0, try to set version=id_ver+2,--nref.
            // No retry: if version changes, the slot is already returned by
            // another one who sees nref:1->0 concurrently; if nref changes,
            // which must be non-zero, the slot will be returned when
            // nref changes from 1->0 again.
            // Example:
            //       stop():  --nref, sees nref:1->0           (1)
            //                try to set version=id_ver+2      (2)
            //    address():  ++nref, unmatched version        (3)
            //                --nref, sees nref:1->0           (4)
            //                try to set version=id_ver+2      (5)
            // 1,2,3,4,5 or 1,3,4,2,5:
            //            stop() succeeds, address() fails at (5).
            // 1,3,2,4,5: stop() fails with (2), the slot will be
            //            returned by (5) of address()
            // 1,3,4,5,2: stop() fails with (2), the slot is already
            //            returned by (5) of address().
            uint64_t expected_vref = vref - 1;
            if (_versioned_ref.compare_exchange_strong(
                        expected_vref, _make_vref(id_ver + 2, 0),
                        butil::memory_order_acquire,
                        butil::memory_order_relaxed)) {
                _on_recycle();
                // We don't return m immediatly when the reference count
                // reaches 0 as there might be in processing tasks. Instead
                // _on_recycle would push a `stop_task' after which is excuted
                // m would be finally returned and reset
                return 1;
            }
            return 0;
        }
        LOG(FATAL) << "Invalid id=" << id;
        return -1;
    }
    LOG(FATAL) << "Over dereferenced id=" << id;
    return -1;
}

}  // namespace bthread

#endif  //BTHREAD_EXECUTION_QUEUE_INL_H
