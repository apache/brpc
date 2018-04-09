// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)
//          Rujie Jiang (jiangrujie@baidu.com)

#include <gflags/gflags.h>                            // DEFINE_int32
#include "butil/compat.h"
#include "butil/fd_utility.h"                         // make_close_on_exec
#include "butil/logging.h"                            // LOG
#include "butil/third_party/murmurhash3/murmurhash3.h"// fmix32
#include "bthread/bthread.h"                          // bthread_start_background
#include "brpc/event_dispatcher.h"
#ifdef BRPC_SOCKET_HAS_EOF
#include "brpc/details/has_epollrdhup.h"
#endif
#include "brpc/reloadable_flags.h"
#if defined(OS_MACOSX)
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

namespace brpc {

DEFINE_int32(event_dispatcher_num, 1, "Number of event dispatcher");

DEFINE_bool(usercode_in_pthread, false, 
            "Call user's callback in pthreads, use bthreads otherwise");

EventDispatcher::EventDispatcher()
    : _epfd(-1)
    , _stop(false)
    , _tid(0)
    , _consumer_thread_attr(BTHREAD_ATTR_NORMAL)
{
#if defined(OS_LINUX)
    _epfd = epoll_create(1024 * 1024);
    if (_epfd < 0) {
        PLOG(FATAL) << "Fail to create epoll";
        return;
    }
#elif defined(OS_MACOSX)
    _epfd = kqueue();
    if (_epfd < 0) {
        PLOG(FATAL) << "Fail to create kqueue";
        return;
    }
#else
    #error Not implemented
#endif
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
#if defined(OS_LINUX)
        LOG(FATAL) << "epoll was not created";
#elif defined(OS_MACOSX)
        LOG(FATAL) << "kqueue was not created";
#endif
        return -1;
    }
    
    if (_tid != 0) {
        LOG(FATAL) << "Already started this dispatcher(" << this 
                   << ") in bthread=" << _tid;
        return -1;
    }

    // Set _consumer_thread_attr before creating epoll/kqueue thread to make sure
    // everyting seems sane to the thread.
    _consumer_thread_attr = (consumer_thread_attr  ?
                             *consumer_thread_attr : BTHREAD_ATTR_NORMAL);

    // Polling thread uses the same attr for consumer threads (NORMAL right
    // now). Previously, we used small stack (32KB) which may be overflowed
    // when the older comlog (e.g. 3.1.85) calls com_openlog_r(). Since this
    // is also a potential issue for consumer threads, using the same attr
    // should be a reasonable solution.
    int rc = bthread_start_background(
        &_tid, &_consumer_thread_attr, RunThis, this);
    if (rc) {
        LOG(FATAL) << "Fail to create epoll/kqueue thread: " << berror(rc);
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
#if defined(OS_LINUX)
        epoll_event evt = { EPOLLOUT,  { NULL } };
        epoll_ctl(_epfd, EPOLL_CTL_ADD, _wakeup_fds[1], &evt);
#elif defined(OS_MACOSX)
        struct kevent kqueue_event;
        EV_SET(&kqueue_event, _wakeup_fds[1], EVFILT_WRITE, EV_ADD | EV_ENABLE,
                    0, 0, NULL);
        kevent(_epfd, &kqueue_event, 1, NULL, 0, NULL);
#endif
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

#if defined(OS_LINUX)
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
#elif defined(OS_MACOSX)
    struct kevent evt;
    //TODO(zhujiashun): add EV_EOF
    EV_SET(&evt, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR,
                0, 0, (void*)socket_id);
    if (kevent(_epfd, &evt, 1, NULL, 0, NULL) < 0) {
        return -1;
    }
    if (pollin) {
        EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
                    0, 0, (void*)socket_id);
        if (kevent(_epfd, &evt, 1, NULL, 0, NULL) < 0) {
            return -1;
        }
    }
#endif
    return 0;
}

int EventDispatcher::RemoveEpollOut(SocketId socket_id, 
                                    int fd, bool pollin) {
#if defined(OS_LINUX)
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
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    if (kevent(_epfd, &evt, 1, NULL, 0, NULL) < 0) {
        return -1;
    }
    if (pollin) {
        EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
                    0, 0, (void*)socket_id);
        return kevent(_epfd, &evt, 1, NULL, 0, NULL);
    }
    return 0;
#endif
    return -1;
}

int EventDispatcher::AddConsumer(SocketId socket_id, int fd) {
    if (_epfd < 0) {
        errno = EINVAL;
        return -1;
    }
#if defined(OS_LINUX)
    epoll_event evt;
    evt.events = EPOLLIN | EPOLLET;
    evt.data.u64 = socket_id;
#ifdef BRPC_SOCKET_HAS_EOF
    evt.events |= has_epollrdhup;
#endif
    return epoll_ctl(_epfd, EPOLL_CTL_ADD, fd, &evt);
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR,
                0, 0, (void*)socket_id);
    return kevent(_epfd, &evt, 1, NULL, 0, NULL);
#endif
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
#if defined(OS_LINUX)
    if (epoll_ctl(_epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        PLOG(WARNING) << "Fail to remove fd=" << fd << " from epfd=" << _epfd;
        return -1;
    }
#elif defined(OS_MACOSX)
    struct kevent evt;
    EV_SET(&evt, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(_epfd, &evt, 1, NULL, 0, NULL);
    EV_SET(&evt, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(_epfd, &evt, 1, NULL, 0, NULL);
#endif
    return 0;
}

void* EventDispatcher::RunThis(void* arg) {
    ((EventDispatcher*)arg)->Run();
    return NULL;
}

void EventDispatcher::Run() {
    while (!_stop) {
#if defined(OS_LINUX)
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
#elif defined(OS_MACOSX)
        struct kevent e[32];
        int n = kevent(_epfd, NULL, 0, e, ARRAY_SIZE(e), NULL);
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
#if defined(OS_LINUX)
            PLOG(FATAL) << "Fail to epoll_wait epfd=" << _epfd;
#elif defined(OS_MACOSX)
            PLOG(FATAL) << "Fail to kqueue epfd=" << _epfd;
#endif
            break;
        }
        for (int i = 0; i < n; ++i) {
#if defined(OS_LINUX)
            if (e[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)
#ifdef BRPC_SOCKET_HAS_EOF
                || (e[i].events & has_epollrdhup)
#endif
                ) {
                // We don't care about the return value.
                Socket::StartInputEvent(e[i].data.u64, e[i].events,
                                        _consumer_thread_attr);
            }
#elif defined(OS_MACOSX)
            if ((e[i].flags & EV_ERROR) || e[i].filter == EVFILT_READ) {
                // We don't care about the return value.
                Socket::StartInputEvent((SocketId)e[i].udata, e[i].filter,
                                        _consumer_thread_attr);
            }
#endif
        }
        for (int i = 0; i < n; ++i) {
#if defined(OS_LINUX)
            if (e[i].events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
                // We don't care about the return value.
                Socket::HandleEpollOut(e[i].data.u64);
            }
#elif defined(OS_MACOSX)
            if ((e[i].flags & EV_ERROR) || e[i].filter == EVFILT_WRITE) {
                // We don't care about the return value.
                Socket::HandleEpollOut((SocketId)e[i].udata);
            }
#endif
        }
    }
}

static EventDispatcher* g_edisp = NULL;
static pthread_once_t g_edisp_once = PTHREAD_ONCE_INIT;

static void StopAndJoinGlobalDispatchers() {
    for (int i = 0; i < FLAGS_event_dispatcher_num; ++i) {
        g_edisp[i].Stop();
        g_edisp[i].Join();
    }
}
void InitializeGlobalDispatchers() {
    g_edisp = new EventDispatcher[FLAGS_event_dispatcher_num];
    for (int i = 0; i < FLAGS_event_dispatcher_num; ++i) {
        const bthread_attr_t attr = FLAGS_usercode_in_pthread ?
            BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
        CHECK_EQ(0, g_edisp[i].Start(&attr));
    }
    // This atexit is will be run before g_task_control.stop() because above
    // Start() initializes g_task_control by creating bthread (to run epoll/kqueue).
    CHECK_EQ(0, atexit(StopAndJoinGlobalDispatchers));
}

EventDispatcher& GetGlobalEventDispatcher(int fd) {
    pthread_once(&g_edisp_once, InitializeGlobalDispatchers);
    if (FLAGS_event_dispatcher_num == 1) {
        return g_edisp[0];
    }
    int index = butil::fmix32(fd) % FLAGS_event_dispatcher_num;
    return g_edisp[index];
}

} // namespace brpc
