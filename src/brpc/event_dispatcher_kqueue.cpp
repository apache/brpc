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


#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>

namespace brpc {

EventDispatcher::EventDispatcher()
    : _event_dispatcher_fd(-1)
    , _stop(false)
    , _tid(0)
    , _thread_attr(BTHREAD_ATTR_NORMAL) {
    _event_dispatcher_fd = kqueue();
    if (_event_dispatcher_fd < 0) {
        PLOG(FATAL) << "Fail to create kqueue";
        return;
    }
    CHECK_EQ(0, butil::make_close_on_exec(_event_dispatcher_fd));

    _wakeup_fds[0] = -1;
    _wakeup_fds[1] = -1;
    if (pipe(_wakeup_fds) != 0) {
        PLOG(FATAL) << "Fail to create pipe";
        return;
    }
}

EventDispatcher::~EventDispatcher() {
    Stop();
    Join();
    if (_event_dispatcher_fd >= 0) {
        close(_event_dispatcher_fd);
        _event_dispatcher_fd = -1;
    }
    if (_wakeup_fds[0] > 0) {
        close(_wakeup_fds[0]);
        close(_wakeup_fds[1]);
    }
}

int EventDispatcher::Start(const bthread_attr_t* thread_attr) {
    if (_event_dispatcher_fd < 0) {
        LOG(FATAL) << "kqueue was not created";
        return -1;
    }
    
    if (_tid != 0) {
        LOG(FATAL) << "Already started this dispatcher(" << this 
                   << ") in bthread=" << _tid;
        return -1;
    }

    // Set _thread_attr before creating kqueue thread to make sure
    // everyting seems sane to the thread.
    _thread_attr = (thread_attr ? *thread_attr : BTHREAD_ATTR_NORMAL);

    //_thread_attr is used in StartInputEvent(), assign flag NEVER_QUIT to it will cause new bthread
    // that created by kevent() never to quit.
    bthread_attr_t kqueue_thread_attr = _thread_attr | BTHREAD_NEVER_QUIT;

    // Polling thread uses the same attr for consumer threads (NORMAL right
    // now). Previously, we used small stack (32KB) which may be overflowed
    // when the older comlog (e.g. 3.1.85) calls com_openlog_r(). Since this
    // is also a potential issue for consumer threads, using the same attr
    // should be a reasonable solution.
    int rc = bthread_start_background(
        &_tid, &kqueue_thread_attr, RunThis, this);
    if (rc) {
        LOG(FATAL) << "Fail to create kqueue thread: " << berror(rc);
        return -1;
    }
    return 0;
}

bool EventDispatcher::Running() const {
    return !_stop  && _event_dispatcher_fd >= 0 && _tid != 0;
}

void EventDispatcher::Stop() {
    _stop = true;

    if (_event_dispatcher_fd >= 0) {
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, _wakeup_fds[1], EVFILT_WRITE, EV_ADD | EV_ENABLE,
                    0, 0, NULL);
        kevent(_event_dispatcher_fd, &kqueue_event, 1, NULL, 0, NULL);
    }
}

void EventDispatcher::Join() {
    if (_tid) {
        bthread_join(_tid, NULL);
        _tid = 0;
    }
}

int EventDispatcher::RegisterEvent(IOEventDataId event_data_id,
                                   int fd, bool pollin) {
    if (_event_dispatcher_fd < 0) {
        errno = EINVAL;
        return -1;
    }

    struct kevent evt;
    //TODO(zhujiashun): add EV_EOF
    EV_SET(&evt, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR,
           0, 0, (void*)event_data_id);
    if (kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL) < 0) {
        return -1;
    }
    if (pollin) {
        EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
               0, 0, (void*)event_data_id);
        if (kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL) < 0) {
            return -1;
        }
    }
    return 0;
}

int EventDispatcher::UnregisterEvent(IOEventDataId event_data_id,
                                     int fd, bool pollin) {
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    if (kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL) < 0) {
        return -1;
    }
    if (pollin) {
        EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
               0, 0, (void*)event_data_id);
        return kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL);
    }
    return 0;
}

int EventDispatcher::AddConsumer(IOEventDataId event_data_id, int fd) {
    if (_event_dispatcher_fd < 0) {
        errno = EINVAL;
        return -1;
    }
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
           0, 0, (void*)event_data_id);
    return kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL);
}

int EventDispatcher::RemoveConsumer(int fd) {
    if (fd < 0) {
        return -1;
    }
    // Removing the consumer from dispatcher before closing the fd because
    // if process was forked and the fd is not marked as close-on-exec,
    // closing does not set reference count of the fd to 0, thus does not
    // remove the fd from kqueue More badly, the fd will not be removable
    // from kqueue again! If the fd was level-triggered and there's data left,
    // kevent will keep returning events of the fd continuously, making
    // program abnormal.
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL);
    EV_SET(&evt, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(_event_dispatcher_fd, &evt, 1, NULL, 0, NULL);
    return 0;
}

void* EventDispatcher::RunThis(void* arg) {
    ((EventDispatcher*)arg)->Run();
    return NULL;
}

void EventDispatcher::Run() {
    while (!_stop) {
        struct kevent e[32];
        int n = kevent(_event_dispatcher_fd, NULL, 0, e, ARRAY_SIZE(e), NULL);
        if (_stop) {
            // EV_SET/kevent should have some sort of memory fencing
            // guaranteeing that we(after kevent) see _stop set before
            // EV_SET
            break;
        }
        if (n < 0) {
            if (EINTR == errno) {
                // We've checked _stop, no wake-up will be missed.
                continue;
            }
            PLOG(FATAL) << "Fail to kqueue epfd=" << _event_dispatcher_fd;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if ((e[i].flags & EV_ERROR) || e[i].filter == EVFILT_READ) {
                // We don't care about the return value.
                CallInputEventCallback((IOEventDataId)e[i].udata,
                                       e[i].filter, _thread_attr);
            }
        }
        for (int i = 0; i < n; ++i) {
            if ((e[i].flags & EV_ERROR) || e[i].filter == EVFILT_WRITE) {
                // We don't care about the return value.
                CallOutputEventCallback((IOEventDataId)e[i].udata,
                                        e[i].filter, _thread_attr);
            }
        }
    }
}

} // namespace brpc
