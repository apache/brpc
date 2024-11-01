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

#include <fcntl.h>
#include "butil/iobuf_profiler.h"
#include "butil/strings/string_number_conversions.h"
#include "butil/file_util.h"
#include "butil/fd_guard.h"
#include "butil/fast_rand.h"
#include "butil/hash.h"
#include <execinfo.h>

extern int BAIDU_WEAK GetStackTrace(void** result, int max_depth, int skip_count);

namespace butil {

namespace iobuf {
extern void* cp(void *__restrict dest, const void *__restrict src, size_t n);
}

// Max and min sleep time for IOBuf profiler consuming thread
// when `_sample_queue' is empty.
const uint32_t IOBufProfiler::MIN_SLEEP_MS = 10;
const uint32_t IOBufProfiler::MAX_SLEEP_MS = 1000;

static pthread_once_t g_iobuf_profiler_info_once = PTHREAD_ONCE_INIT;
static bool g_iobuf_profiler_enabled = false;
static uint g_iobuf_profiler_sample_rate = 100;

// Environment variables:
// 1. ENABLE_IOBUF_PROFILER: set value to 1 to enable IOBuf profiler.
// 2. IOBUF_PROFILER_SAMPLE_RATE: set value between (0, 100] to control sample rate.
static void InitGlobalIOBufProfilerInfo() {
    const char* enabled = getenv("ENABLE_IOBUF_PROFILER");
    g_iobuf_profiler_enabled = enabled && strcmp("1", enabled) == 0 && ::GetStackTrace != NULL;
    if (!g_iobuf_profiler_enabled) {
        return;
    }

    char* rate = getenv("IOBUF_PROFILER_SAMPLE_RATE");
    if (rate) {
        int tmp = 0;
        if (butil::StringToInt(rate, &tmp)) {
            if (tmp > 0 && tmp <= 100) {
                g_iobuf_profiler_sample_rate = tmp;
            } else {
                LOG(ERROR) << "IOBUF_PROFILER_SAMPLE_RATE should be in (0, 100], but get " << rate;
            }
        } else {
            LOG(ERROR) << "IOBUF_PROFILER_SAMPLE_RATE should be a number, but get " << rate;
        }
    }
    LOG(INFO) << "g_iobuf_profiler_sample_rate=" << g_iobuf_profiler_sample_rate;
}

bool IsIOBufProfilerEnabled() {
    pthread_once(&g_iobuf_profiler_info_once, InitGlobalIOBufProfilerInfo);
    return g_iobuf_profiler_enabled;
}

bool IsIOBufProfilerSamplable() {
    pthread_once(&g_iobuf_profiler_info_once, InitGlobalIOBufProfilerInfo);
    if (g_iobuf_profiler_sample_rate == 100) {
        return true;
    }
    return fast_rand_less_than(100) + 1 <= g_iobuf_profiler_sample_rate;
}

size_t IOBufSample::stack_hash_code() const {
    if (nframes == 0) {
        return 0;
    }
    if (_hash_code == 0) {
        _hash_code = SuperFastHash(reinterpret_cast<const char*>(stack),
                                   sizeof(void*) * nframes);
    }
    return _hash_code;
}

IOBufProfiler* IOBufProfiler::GetInstance() {
    return ::Singleton<IOBufProfiler, LeakySingletonTraits<IOBufProfiler>>::get();
}

IOBufProfiler::IOBufProfiler()
    : butil::SimpleThread("IOBufProfiler")
    , _stop(false)
    , _sleep_ms(MIN_SLEEP_MS) {
    if (_stack_map.init(1024) != 0) {
        LOG(WARNING) << "Fail to init _stack_map";
    }
    if (_block_info_map.init(1024) != 0) {
        LOG(WARNING) << "Fail to init _block_info_map";
    }
    Start();
}

IOBufProfiler::~IOBufProfiler() {
    StopAndJoin();
    _block_info_map.clear();
    _stack_map.clear();

    // Clear `_sample_queue'.
    IOBufSample* sample = NULL;
    while (_sample_queue.Dequeue(sample)) {
        IOBufSample::Destroy(sample);
    }

}

void IOBufProfiler::Submit(IOBufSample* s) {
    if (_stop.load(butil::memory_order_relaxed)) {
        return;
    }

    _sample_queue.Enqueue(s);
}

void IOBufProfiler::Dump(IOBufSample* s) {
    do {
        BAIDU_SCOPED_LOCK(_mutex);
        // Categorize the stack.
        IOBufRefSampleSharedPtr stack_sample;
        IOBufRefSampleSharedPtr* stack_ptr = _stack_map.seek(s);
        if (!stack_ptr) {
            stack_sample = IOBufSample::CopyAndSharedWithDestroyer(s);
            stack_sample->block = NULL;
            stack_ptr = &_stack_map[stack_sample.get()];
            *stack_ptr = stack_sample;
        } else {
            (*stack_ptr)->count += s->count;
        }

        BlockInfo* info = _block_info_map.seek(s->block);
        if (info) {
            // Categorize the IOBufSample.
            info->stack_count_map[*stack_ptr] += s->count;
            info->ref += s->count;
            if (info->ref == 0) {
                for (const auto& iter : info->stack_count_map) {
                    IOBufRefSampleSharedPtr* stack_ptr2 = _stack_map.seek(iter.first.get());
                    if (stack_ptr2 && *stack_ptr2) {
                        (*stack_ptr2)->count -= iter.second;
                        if ((*stack_ptr2)->count == 0) {
                            _stack_map.erase(iter.first.get());
                        }
                    }
                }
                _block_info_map.erase(s->block);
                break;
            }
        } else {
            BlockInfo& new_info = _block_info_map[s->block];
            new_info.ref += s->count;
            new_info.stack_count_map[*stack_ptr] = s->count;
        }
    } while (false);
    s->block = NULL;
}

IOBufSample* IOBufSample::Copy(IOBufSample* ref) {
    auto copied = IOBufSample::New();
    copied->block = ref->block;
    copied->count = ref->count;
    copied->_hash_code = ref->_hash_code;
    copied->nframes = ref->nframes;
    iobuf::cp(copied->stack, ref->stack, sizeof(void*) * ref->nframes);
    return copied;
}

IOBufRefSampleSharedPtr IOBufSample::CopyAndSharedWithDestroyer(IOBufSample* ref) {
    return { Copy(ref), detail::Destroyer() };
}

void IOBufProfiler::Flush2Disk(const char* filename) {
    if (!filename) {
        LOG(ERROR) << "Parameter [filename] is NULL";
        return;
    }
    // Serialize contentions in _stack_map into _disk_buf.
    _disk_buf.append("--- contention\ncycles/second=1000000000\n");
    butil::IOBufBuilder os;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        for (auto it = _stack_map.begin(); it != _stack_map.end(); ++it) {
            const IOBufRefSampleSharedPtr& c = it->second;
            if (c->nframes == 0) {
                LOG_EVERY_SECOND(WARNING) << "Invalid stack with nframes=0, count=" << c->count;
                continue;
            }
            os << "0 " << c->count << " @";
            for (int i = 0; i < c->nframes; ++i) {
                os << " " << c->stack[i];
            }
            os << "\n";
        }
    }
    _disk_buf.append(os.buf().movable());

    // Append /proc/self/maps to the end of the contention file, required by
    // pprof.pl, otherwise the functions in sys libs are not interpreted.
    butil::IOPortal mem_maps;
    const butil::fd_guard maps_fd(open("/proc/self/maps", O_RDONLY));
    if (maps_fd >= 0) {
        while (true) {
            ssize_t nr = mem_maps.append_from_file_descriptor(maps_fd, 8192);
            if (nr < 0) {
                if (errno == EINTR) {
                    continue;
                }
                PLOG(ERROR) << "Fail to read /proc/self/maps";
                break;
            }
            if (nr == 0) {
                _disk_buf.append(mem_maps);
                break;
            }
        }
    } else {
        PLOG(ERROR) << "Fail to open /proc/self/maps";
    }
    // Write _disk_buf into _filename
    butil::File::Error error;
    butil::FilePath file_path(filename);
    butil::FilePath dir = file_path.DirName();
    if (!butil::CreateDirectoryAndGetError(dir, &error)) {
        LOG(ERROR) << "Fail to create directory=`" << dir.value()
                   << ": " << error;
        return;
    }

    butil::fd_guard fd(open(filename, O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, 0666));
    if (fd < 0) {
        PLOG(ERROR) << "Fail to open " << filename;
        return;
    }
    // Write once normally, write until empty in the end.
    do {
        ssize_t nw = _disk_buf.cut_into_file_descriptor(fd);
        if (nw < 0) {
            if (errno == EINTR) {
                continue;
            }
            PLOG(ERROR) << "Fail to write into " << filename;
            return;
        }
        LOG(ERROR) << "Write " << nw << " bytes into " << filename;
    } while (!_disk_buf.empty());
}

void IOBufProfiler::StopAndJoin() {
    if (_stop.exchange(true, butil::memory_order_relaxed)) {
        return;
    }
    if (!HasBeenJoined()) {
        Join();
    }
}

void IOBufProfiler::Run() {
    while (!_stop.load(butil::memory_order_relaxed)) {
        Consume();
        ::usleep(_sleep_ms * 1000);
    }
}

void IOBufProfiler::Consume() {
    IOBufSample* sample = NULL;
    bool is_empty = true;
    while (_sample_queue.Dequeue(sample)) {
        Dump(sample);
        IOBufSample::Destroy(sample);
        is_empty = false;
    }

    // If `_sample_queue' is empty, exponential increase in sleep time.
    _sleep_ms = !is_empty ? MIN_SLEEP_MS :
                std::min(_sleep_ms * 2, MAX_SLEEP_MS);
}

void SubmitIOBufSample(IOBuf::Block* block, int64_t ref) {
    if (!IsIOBufProfilerEnabled()) {
        return;
    }

    auto sample = IOBufSample::New();
    sample->block = block;
    sample->count = ref;
    sample->nframes = GetStackTrace(sample->stack, arraysize(sample->stack), 0);
    IOBufProfiler::GetInstance()->Submit(sample);
}

bool IOBufProfilerFlush(const char* filename) {
    if (!filename) {
        LOG(ERROR) << "Parameter [filename] is NULL";
        return false;
    }

    auto profiler = IOBufProfiler::GetInstance();
    profiler->Flush2Disk(filename);
    return true;
}

}