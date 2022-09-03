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


#ifdef BRPC_SOCKET_HAS_EOF
#include "brpc/details/has_epollrdhup.h"
#endif

namespace brpc {

EventDispatcher::EventDispatcher()
    : _epfd(-1)
    , _stop(false)
    , _tid(0)
    , _consumer_thread_attr(BTHREAD_ATTR_NORMAL)
{
    _epfd = epoll_create(1024 * 1024);
    if (_epfd < 0) {
        PLOG(FATAL) << "Fail to create epoll";
        return;
    }
    CHECK_EQ(0, butil::make_close_on_exec(_epfd));

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
    if (_epfd >= 0) {
        close(_epfd);
        _epfd = -1;
    }
    if (_wakeup_fds[0] > 0) {
        close(_wakeup_fds[0]);
        close(_wakeup_fds[1]);
    }
}

int EventDispatcher::Start(const bthread_attr_t* consumer_thread_attr) {
    if (_epfd < 0) {
        LOG(FATAL) << "epoll was not created";
        return -1;
    }
    
    if (_tid != 0) {
        LOG(FATAL) << "Already started this dispatcher(" << this 
                   << ") in bthread=" << _tid;
        return -1;
    }

    // Set _consumer_thread_attr before creating epoll thread to make sure
    // everyting seems sane to the thread.
    _consumer_thread_attr = (consumer_thread_attr  ?
                             *consumer_thread_attr : BTHREAD_ATTR_NORMAL);

    //_consumer_thread_attr is used in StartInputEvent(), assign flag NEVER_QUIT to it will cause new bthread
    // that created by epoll_wait() never to quit.
    bthread_attr_t epoll_thread_attr = _consumer_thread_attr | BTHREAD_NEVER_QUIT;

    // Polling thread uses the same attr for consumer threads (NORMAL right
    // now). Previously, we used small stack (32KB) which may be overflowed
    // when the older comlog (e.g. 3.1.85) calls com_openlog_r(). Since this
    // is also a potential issue for consumer threads, using the same attr
    // should be a reasonable solution.
    int rc = bthread_start_background(
        &_tid, &epoll_thread_attr, RunThis, this);
    if (rc) {
        LOG(FATAL) << "Fail to create epoll thread: " << berror(rc);
        return -1;
    }
    return 0;
}

bool EventDispatcher::Running() const {
    return !_stop  && _epfd >= 0 && _tid != 0;
}

void EventDispatcher::Stop() {
    _stop = true;

    if (_epfd >= 0) {
        epoll_event evt = { EPOLLOUT,  { NULL } };
        epoll_ctl(_epfd, EPOLL_CTL_ADD, _wakeup_fds[1], &evt);
    }
}

void EventDispatcher::Join() {
    if (_tid) {
        bthread_join(_tid, NULL);
        _tid = 0;
    }
}

int EventDispatcher::AddEpollOut(SocketId socket_id, int fd, bool pollin) {
    if (_epfd < 0) {
        errno = EINVAL;
        return -1;
    }

    epoll_event evt;
    evt.data.u64 = socket_id;
    evt.events = EPOLLOUT | EPOLLET;
#ifdef BRPC_SOCKET_HAS_EOF
    evt.events |= has_epollrdhup;
#endif
    if (pollin) {
        evt.events |= EPOLLIN;
        if (epoll_ctl(_epfd, EPOLL_CTL_MOD, fd, &evt) < 0) {
            // This fd has been removed from epoll via `RemoveConsumer',
            // in which case errno will be ENOENT
            return -1;
        }
    } else {
        if (epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &evt) < 0) {
            return -1;
        }
    }
    return 0;
}

int EventDispatcher::RemoveEpollOut(SocketId socket_id, 
                                    int fd, bool pollin) {
    if (pollin) {
        epoll_event evt;
        evt.data.u64 = socket_id;
        evt.events = EPOLLIN | EPOLLET;
#ifdef BRPC_SOCKET_HAS_EOF
        evt.events |= has_epollrdhup;
#endif
        return epoll_ctl(_epfd, EPOLL_CTL_MOD, fd, &evt);
    } else {
        return epoll_ctl(_epfd, EPOLL_CTL_DEL, fd, NULL);
    }
    return -1;
}

int EventDispatcher::AddConsumer(SocketId socket_id, int fd) {
    if (_epfd < 0) {
        errno = EINVAL;
        return -1;
    }
    epoll_event evt;
    evt.events = EPOLLIN | EPOLLET;
    evt.data.u64 = socket_id;
#ifdef BRPC_SOCKET_HAS_EOF
    evt.events |= has_epollrdhup;
#endif
    return epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &evt);
    return -1;
}

int EventDispatcher::RemoveConsumer(int fd) {
    if (fd < 0) {
        return -1;
    }
    // Removing the consumer from dispatcher before closing the fd because
    // if process was forked and the fd is not marked as close-on-exec,
    // closing does not set reference count of the fd to 0, thus does not
    // remove the fd from epoll. More badly, the fd will not be removable
    // from epoll again! If the fd was level-triggered and there's data left,
    // epoll_wait will keep returning events of the fd continuously, making
    // program abnormal.
    if (epoll_ctl(_epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        PLOG(WARNING) << "Fail to remove fd=" << fd << " from epfd=" << _epfd;
        return -1;
    }
    return 0;
}

void* EventDispatcher::RunThis(void* arg) {
    ((EventDispatcher*)arg)->Run();
    return NULL;
}

void EventDispatcher::Run() {
    while (!_stop) {
        epoll_event e[32];
#ifdef BRPC_ADDITIONAL_EPOLL
        // Performance downgrades in examples.
        int n = epoll_wait(_epfd, e, ARRAY_SIZE(e), 0);
        if (n == 0) {
            n = epoll_wait(_epfd, e, ARRAY_SIZE(e), -1);
        }
#else
        const int n = epoll_wait(_epfd, e, ARRAY_SIZE(e), -1);
#endif
        if (_stop) {
            // epoll_ctl/epoll_wait should have some sort of memory fencing
            // guaranteeing that we(after epoll_wait) see _stop set before
            // epoll_ctl.
            break;
        }
        if (n < 0) {
            if (EINTR == errno) {
                // We've checked _stop, no wake-up will be missed.
                continue;
            }
            PLOG(FATAL) << "Fail to epoll_wait epfd=" << _epfd;
            break;
        }
        for (int i = 0; i < n; ++i) {
            if (e[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)
#ifdef BRPC_SOCKET_HAS_EOF
                || (e[i].events & has_epollrdhup)
#endif
                ) {
                // We don't care about the return value.
                Socket::StartInputEvent(e[i].data.u64, e[i].events,
                                        _consumer_thread_attr);
            }
        }
        for (int i = 0; i < n; ++i) {
            if (e[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
                // We don't care about the return value.
                Socket::HandleEpollOut(e[i].data.u64);
            }
        }
    }
}

} // namespace brpc
