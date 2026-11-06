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

#include <stdlib.h>                                  // `atexit`
#include <memory>
#include <gflags/gflags.h>                            // DEFINE_int32
#include "butil/compat.h"
#include "butil/fd_utility.h"                         // make_close_on_exec
#include "butil/logging.h"                            // LOG
#include "butil/third_party/murmurhash3/murmurhash3.h"// fmix32
#include "bvar/latency_recorder.h"                    // bvar::LatencyRecorder
#include "bthread/bthread.h"                          // bthread_start_background
#include "bthread/countdown_event.h"
#include "bthread/mutex.h"
#include "bthread/task_group.h"                        // TaskGroup::address_meta
#include "brpc/event_dispatcher.h"

DECLARE_int32(task_group_ntags);

DECLARE_int32(event_dispatcher_num);

namespace brpc {
DEFINE_bool(event_dispatcher_edisp_unsched, false,
            "Enqueue transport input processing without an urgent bthread switch");
DEFINE_bool(event_dispatcher_use_polling, false,
            "Busy poll epoll/kqueue without sleeping. Set before dispatchers "
            "start; urgent/background input scheduling remains independent.");

DEFINE_bool(usercode_in_pthread, false, 
            "Call user's callback in pthreads, use bthreads otherwise");
DEFINE_bool(usercode_in_coroutine, false,
            "User's callback are run in coroutine, no bthread or pthread blocking call");

static EventDispatcher* g_edisp = nullptr;
static bvar::LatencyRecorder* g_edisp_read_lantency = nullptr;
static bvar::LatencyRecorder* g_edisp_write_lantency = nullptr;
static pthread_once_t g_edisp_once = PTHREAD_ONCE_INIT;
static int g_edisp_ntags = 0;
static int g_edisp_num = 0;

struct EventDispatcherPollingState {
    EventDispatcherPollingState(const PollingCallbacks& cb,
                                 uint64_t generation)
        : callbacks(cb), id(generation) {}
    const PollingCallbacks callbacks;
    const uint64_t id;
};

struct EventDispatcherPollingCommand {
    explicit EventDispatcherPollingCommand(EventDispatcherPollingState* s)
        : state(s), rc(0) {}
    EventDispatcherPollingState* state;
    int rc;
    bthread::CountdownEvent done;
};

struct EventDispatcherPollingGroup {
    // Registration may suspend its caller while dispatchers acknowledge. A
    // pthread mutex held across this wait could deadlock the worker pool.
    bthread::Mutex mutex;
    uint64_t next_id = 0;
    std::unique_ptr<EventDispatcherPollingState> state;
};
static EventDispatcherPollingGroup* g_polling_groups = nullptr;

bool EventDispatcher::IsDispatcherThread() const {
    const bthread_t tid = _tid.load(std::memory_order_acquire);
    return tid != INVALID_BTHREAD && bthread_self() == tid;
}

int EventDispatcher::SetPollingCallbacks(EventDispatcherPollingState* state) {
    EventDispatcherPollingCommand command(state);
    {
        BAIDU_SCOPED_LOCK(_polling_command_mutex);
        if (_polling_finished) {
            return state ? ESHUTDOWN : 0;
        }
        if (state && _stop.load(std::memory_order_acquire)) {
            return ESHUTDOWN;
        }
        CHECK(_polling_command.load(std::memory_order_relaxed) == nullptr);
        _polling_command.store(&command, std::memory_order_release);
    }
    // Only polling dispatchers accept commands, so no wakeup syscall is needed.
    CHECK_EQ(0, command.done.wait());
    return command.rc;
}

void EventDispatcher::ReleasePollingCallbacks() {
    if (_polling_state) {
        if (_polling_state->callbacks.release_fn) {
            _polling_state->callbacks.release_fn(_polling_context);
        }
        _polling_state = nullptr;
        _polling_context.user_data = nullptr;
    }
}

void EventDispatcher::ProcessPollingCommand() {
    // Run() only probes the pointer with a relaxed load. Acquire the command
    // published by SetPollingCallbacks() before accessing its contents.
    EventDispatcherPollingCommand* command =
        _polling_command.exchange(nullptr, std::memory_order_acquire);
    if (!command) {
        return;
    }
    ReleasePollingCallbacks();
    if (command->state) {
        if (_stop.load(std::memory_order_acquire)) {
            command->rc = ESHUTDOWN;
        } else {
            _polling_state = command->state;
            _polling_context.tag = bthread_self_tag();
            _polling_context.dispatcher_index = _priority_index;
            if (_polling_state->callbacks.init_fn) {
                command->rc =
                    _polling_state->callbacks.init_fn(_polling_context);
                if (command->rc != 0) {
                    if (command->rc < 0) {
                        command->rc = EINVAL;
                    }
                    ReleasePollingCallbacks();
                }
            }
        }
    }
    // signal() may resume the caller, which destroys the stack command.
    command->done.signal();
}

void EventDispatcher::RunPollingCallback() {
    if (_polling_state && !_stop.load(std::memory_order_acquire)) {
        _polling_state->callbacks.poll_fn(_polling_context);
    }
}

void EventDispatcher::FinishPolling() {
    // Release first: a concurrent unregister must not return while release_fn
    // still uses the registration. Then reject/acknowledge pending commands.
    ReleasePollingCallbacks();
    EventDispatcherPollingCommand* command;
    {
        BAIDU_SCOPED_LOCK(_polling_command_mutex);
        _polling_finished = true;
        command = _polling_command.exchange(nullptr, std::memory_order_acquire);
        if (command) {
            command->rc = command->state ? ESHUTDOWN : 0;
        }
    }
    // Acknowledgement may switch bthreads; never hold a pthread mutex here.
    if (command) {
        command->done.signal();
    }
}

bool EventDispatcherUnsched() {
    return FLAGS_event_dispatcher_edisp_unsched;
}

static void StopAndJoinGlobalDispatchers() {
    for (int i = 0; i < g_edisp_ntags * g_edisp_num; ++i) {
        g_edisp[i].Stop();
    }
    for (int i = 0; i < g_edisp_ntags * g_edisp_num; ++i) {
        g_edisp[i].Join();
    }
}

void InitializeGlobalDispatchers() {
    // These options define array sizes and fd ownership and are startup-only.
    g_edisp_ntags = FLAGS_task_group_ntags;
    g_edisp_num = FLAGS_event_dispatcher_num;
    CHECK_GT(g_edisp_ntags, 0);
    CHECK_GT(g_edisp_num, 0);
    // A dispatcher may switch bthreads through `bthread_start_urgent()`
    // while handling events. Waiting in `epoll_wait`/`kevent` blocks its worker
    // pthread. Busy polling also occupies a worker. Reserve one extra worker
    // per dispatcher to preserve the normal concurrency configured by
    // `ServerOptions.num_threads` or `-bthread_concurrency`.
    // Without reservations, `event_dispatcher_num >= num_threads` can leave
    // all workers blocked in dispatcher waits, preventing other work.
    for (int i = 0; i < g_edisp_ntags; ++i) {
        bthread_tag_t tag = (BTHREAD_TAG_DEFAULT + i) % g_edisp_ntags;
        int rc = bthread_add_reserved_concurrency_by_tag(g_edisp_num, tag);
        CHECK_EQ(0, rc) << "Fail to reserve " << g_edisp_num
                        << " workers for event dispatchers of tag=" << tag
                        << ": " << berror(rc);
    }
    g_edisp_read_lantency = new bvar::LatencyRecorder("event_dispatcher_read");
    g_edisp_write_lantency = new bvar::LatencyRecorder("event_dispatcher_write");
    g_edisp = new EventDispatcher[g_edisp_ntags * g_edisp_num];
    g_polling_groups = new EventDispatcherPollingGroup[g_edisp_ntags];
    for (int i = 0; i < g_edisp_ntags; ++i) {
        for (int j = 0; j < g_edisp_num; ++j) {
            bthread_attr_t attr =
                FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
            attr.tag = (BTHREAD_TAG_DEFAULT + i) % g_edisp_ntags;
            g_edisp[i * g_edisp_num + j].set_priority_index(j);
            CHECK_EQ(0, g_edisp[i * g_edisp_num + j].Start(&attr));
        }
    }
    // This atexit is will be run before g_task_control.stop() because above
    // Start() initializes g_task_control by creating bthread (to run epoll/kqueue).
    CHECK_EQ(0, atexit(StopAndJoinGlobalDispatchers));
}

EventDispatcher& GetGlobalEventDispatcher(int fd, bthread_tag_t tag) {
    pthread_once(&g_edisp_once, InitializeGlobalDispatchers);
    if (g_edisp_ntags == 1 && g_edisp_num == 1) {
        return g_edisp[0];
    }
    int index = butil::fmix32(fd) % g_edisp_num;
    return g_edisp[tag * g_edisp_num + index];
}

static bool IsGlobalDispatcherThread() {
    for (int i = 0; i < g_edisp_ntags * g_edisp_num; ++i) {
        if (g_edisp[i].IsDispatcherThread()) {
            return true;
        }
    }
    return false;
}

int RegisterPollingCallbacksWithTag(bthread_tag_t tag,
                                   const PollingCallbacks& callbacks,
                                   PollingRegistration* registration) {
    if (!registration || !callbacks.poll_fn || tag < 0 ||
        tag >= FLAGS_task_group_ntags) {
        errno = EINVAL;
        return -1;
    }
    pthread_once(&g_edisp_once, InitializeGlobalDispatchers);
    if (IsGlobalDispatcherThread()) {
        errno = EDEADLK;
        return -1;
    }
    if (tag >= g_edisp_ntags) {
        errno = EINVAL;
        return -1;
    }
    EventDispatcherPollingGroup& group = g_polling_groups[tag];
    BAIDU_SCOPED_LOCK(group.mutex);
    if (group.state) {
        errno = EEXIST;
        return -1;
    }
    EventDispatcher* dispatchers = g_edisp + tag * g_edisp_num;
    if (!dispatchers[0].use_polling()) {
        errno = EPERM;
        return -1;
    }
    std::unique_ptr<EventDispatcherPollingState> state(
        new EventDispatcherPollingState(callbacks, ++group.next_id));
    for (int i = 0; i < g_edisp_num; ++i) {
        const int rc = dispatchers[i].SetPollingCallbacks(state.get());
        if (rc != 0) {
            for (int j = 0; j < i; ++j) {
                CHECK_EQ(0, dispatchers[j].SetPollingCallbacks(nullptr));
            }
            errno = rc;
            return -1;
        }
    }
    registration->_tag = tag;
    registration->_id = state->id;
    group.state = std::move(state);
    return 0;
}

int UnregisterPollingCallbacks(PollingRegistration registration) {
    if (!registration._id || registration._tag < 0) {
        errno = EINVAL;
        return -1;
    }
    pthread_once(&g_edisp_once, InitializeGlobalDispatchers);
    if (IsGlobalDispatcherThread()) {
        errno = EDEADLK;
        return -1;
    }
    if (registration._tag >= g_edisp_ntags) {
        errno = EINVAL;
        return -1;
    }
    EventDispatcherPollingGroup& group = g_polling_groups[registration._tag];
    BAIDU_SCOPED_LOCK(group.mutex);
    if (!group.state || group.state->id != registration._id) {
        errno = ENOENT;
        return -1;
    }
    EventDispatcher* dispatchers =
        g_edisp + registration._tag * g_edisp_num;
    for (int i = 0; i < g_edisp_num; ++i) {
        CHECK_EQ(0, dispatchers[i].SetPollingCallbacks(nullptr));
    }
    group.state.reset();
    return 0;
}

int IOEventData::OnCreated(const IOEventDataOptions& options) {
    if (!options.input_cb) {
        LOG(ERROR) << "Invalid input_cb=NULL";
        return -1;
    }
    if (!options.output_cb) {
        LOG(ERROR) << "Invalid output_cb=NULL";
        return -1;
    }

    _options = options;
    return 0;
}

void IOEventData::BeforeRecycled() {
    _options = { nullptr, nullptr, nullptr };
}

} // namespace brpc

#if defined(OS_LINUX)
    #include "brpc/event_dispatcher_epoll.cpp"
#elif defined(OS_MACOSX)
    #include "brpc/event_dispatcher_kqueue.cpp"
#else
    #error Not implemented
#endif
