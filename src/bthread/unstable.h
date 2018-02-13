// bthread - A M:N threading library to make applications more concurrent.
// Copyright (c) 2012 Baidu, Inc.
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

// Author: Ge,Jun (gejun@baidu.com)
// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_UNSTABLE_H
#define BTHREAD_UNSTABLE_H

#include <pthread.h>
#include <sys/socket.h>
#include "bthread/types.h"
#include "bthread/errno.h"

// NOTICE:
//   As the filename implies, this file lists UNSTABLE bthread functions
//   which are likely to be modified or even removed in future release. We
//   don't guarantee any kind of backward compatibility. Don't use these
//   functions if you're not ready to change your code according to newer
//   versions of bthread.

__BEGIN_DECLS

// Schedule tasks created by BTHREAD_NOSIGNAL
extern void bthread_flush();

// Mark the calling bthread as "about to quit". When the bthread is scheduled,
// worker pthreads are not notified.
extern int bthread_about_to_quit();

// Run `on_timer(arg)' at or after real-time `abstime'. Put identifier of the
// timer into *id.
// Return 0 on success, errno otherwise.
extern int bthread_timer_add(bthread_timer_t* id, timespec abstime,
                             void (*on_timer)(void*), void* arg);

// Unschedule the timer associated with `id'.
// Returns: 0 - exist & not-run; 1 - still running; EINVAL - not exist.
extern int bthread_timer_del(bthread_timer_t id);

// Suspend caller thread until the file descriptor `fd' has `epoll_events'.
// Returns 0 on success, -1 otherwise and errno is set.
// NOTE: Due to an epoll bug(https://patchwork.kernel.org/patch/1970231),
// current implementation relies on EPOLL_CTL_ADD and EPOLL_CTL_DEL which
// are not scalable, don't use bthread_fd_*wait functions in performance
// critical scenario.
extern int bthread_fd_wait(int fd, unsigned epoll_events);

// Suspend caller thread until the file descriptor `fd' has `epoll_events'
// or CLOCK_REALTIME reached `abstime' if abstime is not NULL.
// Returns 0 on success, -1 otherwise and errno is set.
extern int bthread_fd_timedwait(int fd, unsigned epoll_events,
                                const timespec* abstime);

// Close file descriptor `fd' and wake up all threads waiting on it.
// User should call this function instead of close(2) if bthread_fd_wait,
// bthread_fd_timedwait, bthread_connect were called on the file descriptor,
// otherwise waiters will suspend indefinitely and bthread's internal epoll
// may work abnormally after fork() is called.
// NOTE: This function does not wake up pthread waiters.(tested on linux 2.6.32)
extern int bthread_close(int fd);

// Replacement of connect(2) in bthreads.
extern int bthread_connect(int sockfd, const sockaddr* serv_addr,
                           socklen_t addrlen);

// Add a startup function that each pthread worker will run at the beginning
// To run code at the end, use butil::thread_atexit()
// Returns 0 on success, error code otherwise.
extern int bthread_set_worker_startfn(void (*start_fn)());

// Stop all bthread and worker pthreads.
// You should avoid calling this function which may cause bthread after main()
// suspend indefinitely.
extern void bthread_stop_world();

// Create a bthread_key_t with an additional arg to destructor.
// Generally the dtor_arg is for passing the creator of data so that we can
// return the data back to the creator in destructor. Without this arg, we
// have to do an extra heap allocation to contain data and its creator.
extern int bthread_key_create2(bthread_key_t* key,
                               void (*destructor)(void* data, const void* dtor_arg),
                               const void* dtor_arg);

// CAUTION: functions marked with [PRC INTERNAL] are NOT supposed to be called
// by RPC users.

// [RPC INTERNAL]
// Create a pool to cache KeyTables so that frequently created/destroyed
// bthreads reuse these tables, namely when a bthread needs a KeyTable,
// it fetches one from the pool instead of creating on heap. When a bthread
// exits, it puts the table back to pool instead of deleting it.
// Returns 0 on success, error code otherwise.
extern int bthread_keytable_pool_init(bthread_keytable_pool_t*);

// [RPC INTERNAL]
// Destroy the pool. All KeyTables inside are destroyed.
// Returns 0 on success, error code otherwise.
extern int bthread_keytable_pool_destroy(bthread_keytable_pool_t*);

// [RPC INTERNAL]
// Put statistics of `pool' into `stat'.
extern int bthread_keytable_pool_getstat(bthread_keytable_pool_t* pool,
                                         bthread_keytable_pool_stat_t* stat);

// [RPC INTERNAL]
// Reserve at most `nfree' keytables with `key' pointing to data created by
// ctor(args).
extern void bthread_keytable_pool_reserve(
    bthread_keytable_pool_t* pool, size_t nfree,
    bthread_key_t key, void* ctor(const void* args), const void* args);

__END_DECLS

#endif  // BTHREAD_UNSTABLE_H
