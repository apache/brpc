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

#include "butil/macros.h"                     // DISALLOW_COPY_AND_ASSIGN
#include "bthread/types.h"                   // bthread_t, bthread_attr_t
#include "brpc/versioned_ref_with_id.h"


namespace brpc {

// Unique identifier of a EventData.
// Users shall store EventDataId instead of EventData and call EventData::Address()
// to convert the identifier to an unique_ptr at each access. Whenever a
// unique_ptr is not destructed, the enclosed EventData will not be recycled.
typedef VRefId EventDataId;

const VRefId INVALID_EVENT_DATA_ID = INVALID_VREF_ID;

class EventData;

typedef VersionedRefWithIdUniquePtr<EventData> EventDataUniquePtr;

// User callback type of input event and output event.
typedef int (*InputEventCallback) (VRefId id, uint32_t events,
                                   const bthread_attr_t& thread_attr);
typedef InputEventCallback OutputEventCallback;

struct EventDataOptions {
    // Find user object to handle event by `user_id'.
    uint64_t user_id;
    // Callback for input event.
    InputEventCallback input_cb;
    // Callback for output event.
    OutputEventCallback output_cb;
};

// EventDispatcher finds EventData by EventDataId which is
// stored in epoll/kqueue data, and calls its callback, so
// EventDispatcher supports various IO types, such as socket,
// pipe, eventfd, timerfd, etc.
class EventData : public VersionedRefWithId<EventData, EventDataOptions> {
public:
    explicit EventData(Forbidden f)
        : VersionedRefWithId<EventData, EventDataOptions>(f)
        , _options{INVALID_EVENT_DATA_ID, NULL, NULL}
    {}

    DISALLOW_COPY_AND_ASSIGN(EventData);

    int CallInputEventCallback(uint32_t events,
                               const bthread_attr_t& thread_attr) {
        return _options.input_cb(_options.user_id, events, thread_attr);
    }

    int CallOutputEventCallback(uint32_t events,
                                const bthread_attr_t& thread_attr) {
        return _options.output_cb(_options.user_id, events, thread_attr);
    }

private:
    int OnCreate(const EventDataOptions* options) override;
    void OnRecycle() override;

    EventDataOptions _options;
};

void MakeEventDataIdInvalid(EventDataId& id);

namespace rdma {
class RdmaEndpoint;
}

// Dispatch edge-triggered events of file descriptors to consumers
// running in separate bthreads.
class EventDispatcher {
friend class Socket;
friend class rdma::RdmaEndpoint;
public:
    EventDispatcher();
    
    virtual ~EventDispatcher();

    // Start this dispatcher in a bthread.
    // Use |*consumer_thread_attr| (if it's not NULL) as the attribute to
    // create bthreads running user callbacks.
    // Returns 0 on success, -1 otherwise.
    virtual int Start(const bthread_attr_t* thread_attr);

    // True iff this dispatcher is running in a bthread
    bool Running() const;

    // Stop bthread of this dispatcher.
    void Stop();

    // Suspend calling thread until bthread of this dispatcher stops.
    void Join();

    // When edge-triggered events happen on `fd', call
    // `on_edge_triggered_events' of `socket_id'.
    // Notice that this function also transfers ownership of `socket_id',
    // When the file descriptor is removed from internal epoll, the Socket
    // will be dereferenced once additionally.
    // Returns 0 on success, -1 otherwise.
    int AddConsumer(EventDataId event_data_id, int fd);

    // Watch EPOLLOUT event on `fd' into epoll device. If `pollin' is
    // true, EPOLLIN event will also be included and EPOLL_CTL_MOD will
    // be used instead of EPOLL_CTL_ADD. When event arrives,
    // `Socket::HandleEpollOut' will be called with `socket_id'
    // Returns 0 on success, -1 otherwise and errno is set
    int RegisterEvent(EventDataId event_data_id, int fd, bool pollin);
    
    // Remove EPOLLOUT event on `fd'. If `pollin' is true, EPOLLIN event
    // will be kept and EPOLL_CTL_MOD will be used instead of EPOLL_CTL_DEL
    // Returns 0 on success, -1 otherwise and errno is set
    int UnregisterEvent(EventDataId event_data_id, int fd, bool pollin);

private:
    DISALLOW_COPY_AND_ASSIGN(EventDispatcher);

    // Calls Run()
    static void* RunThis(void* arg);

    // Thread entry.
    void Run();

    // Remove the file descriptor `fd' from epoll.
    int RemoveConsumer(int fd);

    // Call user callback of input event and output event.
    static int CallInputEventCallback(EventDataId event_data_id,
                                      uint32_t events,
                                      const bthread_attr_t& thread_attr);
    static int CallOutputEventCallback(EventDataId event_data_id,
                                       uint32_t events,
                                       const bthread_attr_t& thread_attr);

    // The epoll/kqueue fd to watch events.
    int _event_dispatcher_fd;

    // false unless Stop() is called.
    volatile bool _stop;

    // identifier of hosting bthread
    bthread_t _tid;

    // The attribute of bthreads calling user callbacks.
    bthread_attr_t _thread_attr;

    // Pipe fds to wakeup EventDispatcher from `epoll_wait' in order to quit
    int _wakeup_fds[2];
};

EventDispatcher& GetGlobalEventDispatcher(int fd, bthread_tag_t tag);

} // namespace brpc


#endif  // BRPC_EVENT_DISPATCHER_H
