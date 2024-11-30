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

#ifndef BIGO_BRPC_IOBUF_PROFILER_H
#define BIGO_BRPC_IOBUF_PROFILER_H

#include <unordered_map>
#include <unordered_set>
#include "butil/iobuf.h"
#include "butil/object_pool.h"
#include "butil/threading/simple_thread.h"
#include "butil/threading/simple_thread.h"
#include "butil/memory/singleton.h"
#include "butil/containers/flat_map.h"
#include "butil/containers/mpsc_queue.h"

namespace butil {

struct IOBufSample;
typedef std::shared_ptr<IOBufSample> IOBufRefSampleSharedPtr;

struct IOBufSample {
    IOBufSample* next;
    IOBuf::Block* block;
    int64_t count; // // reference count of the block.
    void* stack[28]; // backtrace.
    int nframes; // num of frames in stack.

    static IOBufSample* New() {
        return get_object<IOBufSample>();
    }

    static IOBufSample* Copy(IOBufSample* ref);
    static IOBufRefSampleSharedPtr CopyAndSharedWithDestroyer(IOBufSample* ref);
    static void Destroy(IOBufSample* ref) {
        ref->_hash_code = 0;
        return_object(ref);
    }

    size_t stack_hash_code() const;

private:
friend ObjectPool<IOBufSample>;

    IOBufSample()
        : next(NULL)
        , block(NULL)
        , count(0)
        , stack{}
        , nframes(0)
        , _hash_code(0) {}

    ~IOBufSample() = default;

    mutable uint32_t _hash_code; // For combining samples with hashmap.
};

BAIDU_CASSERT(sizeof(IOBufSample) == 256, be_friendly_to_allocator);

namespace detail {
// Functor to compare IOBufRefSample.
template <typename T>
struct IOBufSampleEqual {
    bool operator()(const T& c1, const T& c2) const {
        return c1->stack_hash_code() ==c2->stack_hash_code() &&
            c1->nframes == c2->nframes &&
            memcmp(c1->stack, c2->stack, sizeof(void*) * c1->nframes) == 0;
    }
};

// Functor to hash IOBufRefSample.
template <typename T>
struct IOBufSampleHash {
    size_t operator()(const T& c) const {
        return c->stack_hash_code();
    }
};

struct Destroyer {
    void operator()(IOBufSample* ref) const {
        if (ref) {
            IOBufSample::Destroy(ref);
        }
    }
};
}

class IOBufProfiler : public butil::SimpleThread {
public:
    static IOBufProfiler* GetInstance();

    // Submit the IOBuf sample along with stacktrace.
    void Submit(IOBufSample* s);
    // Dump IOBuf sample to map.
    void Dump(IOBufSample* s);
    // Write buffered data into resulting file.
    void Flush2Disk(const char* filename);

    void StopAndJoin();

private:
    friend struct DefaultSingletonTraits<IOBufProfiler>;

    typedef butil::FlatMap<IOBufSample*,
                           IOBufRefSampleSharedPtr,
                           detail::IOBufSampleHash<IOBufSample*>,
                           detail::IOBufSampleEqual<IOBufSample*>> IOBufRefMap;

    // <iobuf stack, ref count>
    typedef butil::FlatMap<IOBufRefSampleSharedPtr,
                           int64_t,
                           detail::IOBufSampleHash<IOBufRefSampleSharedPtr>,
                           detail::IOBufSampleEqual<IOBufRefSampleSharedPtr>> StackCountMap;
    struct BlockInfo {
        int64_t ref{0};
        StackCountMap stack_count_map;
    };
    typedef butil::FlatMap<IOBuf::Block*, BlockInfo> BlockInfoMap;

    IOBufProfiler();
    ~IOBufProfiler() override;
    DISALLOW_COPY_AND_ASSIGN(IOBufProfiler);

    void Run() override;
    // Consume the IOBuf sample in _sample_queue.
    void Consume();

    // Stop flag of IOBufProfiler.
    butil::atomic<bool> _stop;
    // IOBuf sample queue.
    MPSCQueue<IOBufSample*> _sample_queue;

    // Temp buf before saving the file.
    butil::IOBuf _disk_buf;
    // Combining same samples to make result smaller.
    IOBufRefMap _stack_map;
    // Record block info.
    BlockInfoMap _block_info_map;
    Mutex _mutex;

    // Sleep when `_sample_queue' is empty.
    uint32_t _sleep_ms;
    static const uint32_t MIN_SLEEP_MS;
    static const uint32_t MAX_SLEEP_MS;
};

bool IsIOBufProfilerEnabled();
bool IsIOBufProfilerSamplable();

void SubmitIOBufSample(IOBuf::Block* block, int64_t ref);

bool IOBufProfilerFlush(const char* filename);

}
#endif //BIGO_BRPC_IOBUF_PROFILER_H
