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


#ifndef BRPC_EVENT_DISPATCHER_H
#define BRPC_EVENT_DISPATCHER_H

#include <atomic>
#include <functional>
#include <gflags/gflags_declare.h>          // DECLARE_bool
#include "butil/synchronization/lock.h"
#include "butil/macros.h"                     // DISALLOW_COPY_AND_ASSIGN
#include "bthread/types.h"                   // bthread_t, bthread_attr_t
#include "brpc/versioned_ref_with_id.h"


namespace brpc {

DECLARE_bool(event_dispatcher_edisp_unsched);
DECLARE_bool(event_dispatcher_use_polling);

// Each dispatcher owns a separate context. A bthread may migrate between
// worker pthreads: this context is dispatcher-local, not pthread-local.
struct PollingContext {
    bthread_tag_t tag = BTHREAD_TAG_INVALID;
    int dispatcher_index = -1;
    void* user_data = nullptr;
};

struct PollingCallbacks {
    // Return 0 on success or a positive errno value on failure.
    std::function<int(PollingContext&)> init_fn;
    // Called inline after each event batch, including batches with no events.
    std::function<void(PollingContext&)> poll_fn;
    // Also called after failed initialization to clean up partial resources.
    std::function<void(PollingContext&)> release_fn;
};

// An opaque token. Copies refer to the same registration; a stale token cannot
// unregister a newer registration on the same tag.
class PollingRegistration {
public:
    PollingRegistration() : _tag(BTHREAD_TAG_INVALID), _id(0) {}

private:
    friend int RegisterPollingCallbacksWithTag(
        bthread_tag_t, const PollingCallbacks&, PollingRegistration*);
    friend int UnregisterPollingCallbacks(PollingRegistration);
    bthread_tag_t _tag;
    uint64_t _id;
};

// Requires event_dispatcher_use_polling to be set before dispatchers start.
// Register one callback set on all dispatchers of the tag, even if they have
// already started. Returns only after all init_fn calls complete. Failure rolls
// back all initialized contexts. Duplicate registration fails with EEXIST.
// Callbacks are serial per dispatcher, but concurrent across dispatchers. They
// must not block, throw, or synchronously register/unregister callbacks (EDEADLK).
// poll_fn must be non-null. Returns 0 on success, -1 with errno on failure.
int RegisterPollingCallbacksWithTag(bthread_tag_t tag,
                                   const PollingCallbacks& callbacks,
                                   PollingRegistration* registration);

// On success, all callbacks have stopped and release_fn has completed. This
// does not stop dispatchers or TCP traffic. Returns 0 or -1 with errno.
int UnregisterPollingCallbacks(PollingRegistration registration);

struct EventDispatcherPollingState;
struct EventDispatcherPollingCommand;

// Unique identifier of a IOEventData.
// Users shall store EventDataId instead of EventData and call EventData::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed EventData will not be recycled.
typedef VRefId IOEventDataId;

const VRefId INVALID_IO_EVENT_DATA_ID = INVALID_VREF_ID;

class IOEventData;

typedef VersionedRefWithIdUniquePtr<IOEventData> EventDataUniquePtr;

// User callback type of input event and output event.
typedef int (*InputEventCallback) (void* id, uint32_t events,
                                   const bthread_attr_t& thread_attr);
typedef InputEventCallback OutputEventCallback;

struct IOEventDataOptions {
    // Callback for input event.
    InputEventCallback input_cb;
    // Callback for output event.
    OutputEventCallback output_cb;
    // User data.
    void* user_data;
};

// EventDispatcher finds IOEventData by IOEventDataId which is
// stored in epoll/kqueue data, and calls input/output event callback,
// so EventDispatcher supports various IO types, such as socket,
// pipe, eventfd, timerfd, etc.
class IOEventData : public VersionedRefWithId<IOEventData> {
public:
    explicit IOEventData(Forbidden f)
        : VersionedRefWithId<IOEventData>(f)
        , _options{ nullptr, nullptr, nullptr } {}

    DISALLOW_COPY_AND_ASSIGN(IOEventData);

    int CallInputEventCallback(uint32_t events,
                               const bthread_attr_t& thread_attr) {
        return _options.input_cb(_options.user_data, events, thread_attr);
    }

    int CallOutputEventCallback(uint32_t events,
                                const bthread_attr_t& thread_attr) {
        return _options.output_cb(_options.user_data, events, thread_attr);
    }

private:
friend class VersionedRefWithId<IOEventData>;

    int OnCreated(const IOEventDataOptions& options);
    void BeforeRecycled();

    IOEventDataOptions _options;
};

namespace rdma {
class RdmaEndpoint;
}

// Dispatch edge-triggered events of file descriptors to consumers.
// By default callbacks run in spawned bthreads; when usercode-in-coroutine is
// enabled, the callback may run inline in the current coroutine.
class EventDispatcher {
friend class Socket;
friend class rdma::RdmaEndpoint;
template <typename T> friend class IOEvent;
friend int RegisterPollingCallbacksWithTag(
    bthread_tag_t, const PollingCallbacks&, PollingRegistration*);
friend int UnregisterPollingCallbacks(PollingRegistration);
public:
    EventDispatcher();
    
    virtual ~EventDispatcher();

    // Start this dispatcher in a bthread.
    // Use |*thread_attr| (if it's not nullptr) as the attribute to
    // create bthreads running user callbacks.
    // Returns 0 on success, -1 otherwise.
    virtual int Start(const bthread_attr_t* thread_attr);

    // True iff this dispatcher is running in a bthread
    bool Running() const;

    // Waiting policy is captured by Start(), independently of the urgent /
    // background input-processing policy controlled by EventDispatcherUnsched.
    bool use_polling() const { return _use_polling; }

    bool IsDispatcherThread() const;

    // Stop bthread of this dispatcher.
    void Stop();

    void set_priority_index(int idx) { _priority_index = idx; }

    // Suspend calling thread until bthread of this dispatcher stops.
    void Join();

    // When edge-triggered events happen on `fd', call
    // `on_edge_triggered_events' of `socket_id'.
    // Notice that this function also transfers ownership of `socket_id',
    // When the file descriptor is removed from internal epoll, the Socket
    // will be dereferenced once additionally.
    // Returns 0 on success, -1 otherwise.
    int AddConsumer(IOEventDataId event_data_id, int fd);

    // Watch EPOLLOUT event on `fd' into epoll device. If `pollin' is
    // true, EPOLLIN event will also be included and EPOLL_CTL_MOD will
    // be used instead of EPOLL_CTL_ADD. When event arrives,
    // `Socket::HandleEpollOut' will be called with `socket_id'
    // Returns 0 on success, -1 otherwise and errno is set
    int RegisterEvent(IOEventDataId event_data_id, int fd, bool pollin);
    
    // Remove EPOLLOUT event on `fd'. If `pollin' is true, EPOLLIN event
    // will be kept and EPOLL_CTL_MOD will be used instead of EPOLL_CTL_DEL
    // Returns 0 on success, -1 otherwise and errno is set
    int UnregisterEvent(IOEventDataId event_data_id, int fd, bool pollin);

private:
    DISALLOW_COPY_AND_ASSIGN(EventDispatcher);

    // Calls Run()
    static void* RunThis(void* arg);

    // Thread entry.
    void Run();

    // Registration is serialized outside the hot path. Only the dispatcher
    // modifies its callbacks/context; commands synchronously acknowledge this.
    int SetPollingCallbacks(EventDispatcherPollingState* state);
    // Slow path: called only when Run() observes a pending command.
    void ProcessPollingCommand();
    void RunPollingCallback();
    void ReleasePollingCallbacks();
    void FinishPolling();

    // Remove the file descriptor `fd' from epoll.
    int RemoveConsumer(int fd);

    // Call user callback of input event and output event.
    template<bool IsInputEvent>
    static int OnEvent(IOEventDataId event_data_id, uint32_t events,
                       const bthread_attr_t& thread_attr) {
        EventDataUniquePtr data;
        if (IOEventData::Address(event_data_id, &data) != 0) {
            return -1;
        }
        return IsInputEvent ?
               data->CallInputEventCallback(events, thread_attr) :
               data->CallOutputEventCallback(events, thread_attr);
    }

    static int CallInputEventCallback(IOEventDataId event_data_id,
                                      uint32_t events,
                                      const bthread_attr_t& thread_attr) {
        return OnEvent<true>(event_data_id, events, thread_attr);
    }

    static int CallOutputEventCallback(IOEventDataId event_data_id,
                                       uint32_t events,
                                       const bthread_attr_t& thread_attr) {
        return OnEvent<false>(event_data_id, events, thread_attr);
    }

    // The epoll/kqueue fd to watch events.
    int _event_dispatcher_fd;

    // false unless Stop() is called. Polling must not rely on syscall fences.
    std::atomic<bool> _stop;

    // identifier of hosting bthread
    std::atomic<bthread_t> _tid;

    // The attribute of bthreads calling user callbacks.
    bthread_attr_t _thread_attr;

    // Pipe fds to wakeup EventDispatcher from `epoll_wait' in order to quit
    int _wakeup_fds[2];

    int _priority_index{-1};

    bool _use_polling{false};
    butil::Mutex _polling_command_mutex;
    bool _polling_finished{false};
    std::atomic<EventDispatcherPollingCommand*> _polling_command{nullptr};
    EventDispatcherPollingState* _polling_state{nullptr};
    PollingContext _polling_context;
};

EventDispatcher& GetGlobalEventDispatcher(int fd, bthread_tag_t tag);

// Unified unsched switch for transport layer.
// false -> urgent start (foreground scheduling before caller continues),
// true  -> background start (enqueue input processing without switching away).
bool EventDispatcherUnsched();

// IOEvent class manages the IO events of a file descriptor conveniently.
template <typename T>
class IOEvent {
public:
    IOEvent()
        : _init(false)
        , _event_data_id(INVALID_IO_EVENT_DATA_ID)
        , _bthread_tag(bthread_self_tag()) {}

    ~IOEvent() { Reset(); }

    DISALLOW_COPY_AND_ASSIGN(IOEvent);

    int Init(void* user_data) {
        if (_init) {
            LOG(WARNING) << "IOEvent has been initialized";
            return 0;
        }
        IOEventDataOptions options{ OnInputEvent, OnOutputEvent, user_data };
        if (IOEventData::Create(&_event_data_id, options) != 0) {
            LOG(ERROR) << "Fail to create EventData";
            return -1;
        }
        _init = true;
        return 0;
    }

    void Reset() {
        if (_init) {
            IOEventData::SetFailedById(_event_data_id);
            _init = false;
        }
    }

    // See comments of `EventDispatcher::AddConsumer'.
    int AddConsumer(int fd) {
        if (!_init) {
            LOG(ERROR) << "IOEvent has not been initialized";
            return -1;
        }
        return GetGlobalEventDispatcher(fd, _bthread_tag)
            .AddConsumer(_event_data_id, fd);
    }

    // See comments of `EventDispatcher::RemoveConsumer'.
    int RemoveConsumer(int fd) {
        if (!_init) {
            LOG(ERROR) << "IOEvent has not been initialized";
            return -1;
        }
        return GetGlobalEventDispatcher(fd, _bthread_tag).RemoveConsumer(fd);
    }

    // See comments of `EventDispatcher::RegisterEvent'.
    int RegisterEvent(int fd, bool pollin) {
        if (!_init) {
            LOG(ERROR) << "IOEvent has not been initialized";
            return -1;
        }
        return GetGlobalEventDispatcher(fd, _bthread_tag)
            .RegisterEvent(_event_data_id, fd, pollin);
    }

    // See comments of `EventDispatcher::UnregisterEvent'.
    int UnregisterEvent(int fd, bool pollin) {
        if (!_init) {
            LOG(ERROR) << "IOEvent has not been initialized";
            return -1;
        }
        return GetGlobalEventDispatcher(fd, _bthread_tag)
            .UnregisterEvent(_event_data_id, fd, pollin);
    }

    void set_bthread_tag(bthread_tag_t bthread_tag) {
        _bthread_tag = bthread_tag;
    }
    bthread_tag_t bthread_tag() const {
        return _bthread_tag;
    }

private:
    // Generic callback to handle input event.
    static int OnInputEvent(void* user_data, uint32_t events,
                            const bthread_attr_t& thread_attr) {
        static_assert(
            butil::is_result_int<decltype(&T::OnInputEvent),
                                 void*, uint32_t,
                                 bthread_attr_t>::value,
            "T::OnInputEvent signature mismatch");
        return T::OnInputEvent(user_data, events, thread_attr);
    }

    // Generic callback to handle output event.
    static int OnOutputEvent(void* user_data, uint32_t events,
                             const bthread_attr_t& thread_attr) {
        static_assert(
            butil::is_result_int<decltype(&T::OnOutputEvent),
                                 void*, uint32_t,
                                 bthread_attr_t>::value,
            "T::OnInputEvent signature mismatch");
        return T::OnOutputEvent(user_data, events, thread_attr);
    }

    bool _init;
    IOEventDataId _event_data_id;
    bthread_tag_t _bthread_tag;
};

} // namespace brpc


#endif  // BRPC_EVENT_DISPATCHER_H
