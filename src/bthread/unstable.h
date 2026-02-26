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

// bthread - An M:N threading library to make applications more concurrent.

// Date: Tue Jul 10 17:40:58 CST 2012

#ifndef BTHREAD_UNSTABLE_H
#define BTHREAD_UNSTABLE_H

#include <stddef.h>
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
extern int bthread_timer_add(bthread_timer_t* id, struct timespec abstime,
                             void (*on_timer)(void*), void* arg);

// Unschedule the timer associated with `id'.
// Returns: 0 - exist & not-run; 1 - still running; EINVAL - not exist.
extern int bthread_timer_del(bthread_timer_t id);

// Suspend caller thread until the file descriptor `fd' has `epoll_events'.
// Returns 0 on success, -1 otherwise and errno is set.
// NOTE: Due to an epoll
// bug(https://web.archive.org/web/20150423184820/https://patchwork.kernel.org/patch/1970231/),
// current implementation relies on EPOLL_CTL_ADD and EPOLL_CTL_DEL which
// are not scalable, don't use bthread_fd_*wait functions in performance
// critical scenario.
extern int bthread_fd_wait(int fd, unsigned events);

// Suspend caller thread until the file descriptor `fd' has `epoll_events'
// or CLOCK_REALTIME reached `abstime' if abstime is not NULL.
// Returns 0 on success, -1 otherwise and errno is set.
extern int bthread_fd_timedwait(int fd, unsigned epoll_events,
                                const struct timespec* abstime);

// Close file descriptor `fd' and wake up all threads waiting on it.
// User should call this function instead of close(2) if bthread_fd_wait,
// bthread_fd_timedwait, bthread_connect were called on the file descriptor,
// otherwise waiters will suspend indefinitely and bthread's internal epoll
// may work abnormally after fork() is called.
// NOTE: This function does not wake up pthread waiters.(tested on linux 2.6.32)
extern int bthread_close(int fd);

// Replacement of connect(2) in bthreads.
extern int bthread_connect(int sockfd, const struct sockaddr* serv_addr,
                           socklen_t addrlen);
// Suspend caller thread until connect(2) on `sockfd' succeeds
// or CLOCK_REALTIME reached `abstime' if `abstime' is not NULL.
extern int bthread_timed_connect(int sockfd, const struct sockaddr* serv_addr,
                                 socklen_t addrlen, const timespec* abstime);

// Add a startup function that each pthread worker will run at the beginning
// To run code at the end, use butil::thread_atexit()
// Returns 0 on success, error code otherwise.
extern int bthread_set_worker_startfn(void (*start_fn)());

// Add a startup function with tag
extern int bthread_set_tagged_worker_startfn(void (*start_fn)(bthread_tag_t));

// Add a create span function
extern int bthread_set_create_span_func(void* (*func)());

// Stop all bthread and worker pthreads.
// You should avoid calling this function which may cause bthread after main()
// suspend indefinitely.
extern void bthread_stop_world();

// Active task callback context. This structure is only valid during callbacks
// registered by bthread_register_active_task_type().
typedef struct bthread_active_task_ctx_t {
    size_t struct_size;
    bthread_tag_t tag;
    uint32_t worker_index;
    pthread_t worker_pthread;
    int32_t bound_cpu;  // -1 when unknown or not bound.
    uint32_t reserved0;
    void* impl;  // internal opaque pointer, pass only to active-task within APIs.
} bthread_active_task_ctx_t;

typedef struct bthread_active_task_type_t {
    size_t struct_size;
    const char* name;
    void* user_data;

    // Called once for each worker pthread after TaskGroup is created and
    // tls_task_group is set, before entering the worker scheduling loop.
    int (*worker_init)(void** worker_local,
                       const bthread_active_task_ctx_t* ctx,
                       void* user_data);

    // Called once for each worker pthread before the worker exits.
    void (*worker_destroy)(void* worker_local,
                           const bthread_active_task_ctx_t* ctx,
                           void* user_data);

    // Called by the worker scheduler loop as a non-blocking maintenance hook to
    // harvest completions and wake local waiters. The runtime may call this
    // hook in multiple places (for example before parking, after wakeup and
    // periodic busy-loop polling). Return 1 to ask the worker loop to retry
    // without entering ParkingLot wait in the current iteration, return 0
    // otherwise.
    int (*harvest)(
        void* worker_local, const bthread_active_task_ctx_t* ctx);
} bthread_active_task_type_t;

// Register an active-task type. This function must be called before bthread
// TaskControl is initialized.
extern int bthread_register_active_task_type(
    const bthread_active_task_type_t* type);

// Active-task callbacks are constrained to simple non-blocking maintenance
// logic (for example completion harvesting + local waiter wakeup). Creating
// new bthreads from active-task callbacks is intentionally unsupported.
//
// Wake the single waiter on `butex' from inside the current active-task
// callback and enqueue the resumed bthread into the current hook worker's
// local queue explicitly (nosignal=true, no immediate switch inside hook).
//
// This API is designed for per-request private butex usage:
// - 0 waiter on `butex': return 0
// - 1 waiter and it is a same-TaskControl/same-tag bthread waiter: return 1
// - otherwise (multiple waiters / pthread waiter / cross-tag / cross-control):
//   return -1 and set errno=EINVAL
//
// Calling this API outside active-task harvest callbacks returns -1 and sets
// errno=EPERM.
extern int bthread_butex_wake_within(const bthread_active_task_ctx_t* ctx,
                                     void* butex);

// Wait on butex with an implicit wait-scope local pin. Semantics are the same
// as butex_wait (including timeout/interruption), but the runtime temporarily
// pins the current bthread to the current worker for this wait operation so the
// resumed bthread is routed back to the home worker and is not stealable before
// bthread_butex_wait_local() returns.
//
// Returns 0 on success, -1 otherwise and errno is set.
// - EPERM: not running inside a normal bthread worker task
extern int bthread_butex_wait_local(void* butex, int expected_value,
                                    const struct timespec* abstime);

// Create a bthread_key_t with an additional arg to destructor.
// Generally the dtor_arg is for passing the creator of data so that we can
// return the data back to the creator in destructor. Without this arg, we
// have to do an extra heap allocation to contain data and its creator.
extern int bthread_key_create2(bthread_key_t* key,
                               void (*destructor)(void* data, const void* dtor_arg),
                               const void* dtor_arg);

// CAUTION: functions marked with [RPC INTERNAL] are NOT supposed to be called
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
// Return thread local keytable list length if exist.
extern int get_thread_local_keytable_list_length(bthread_keytable_pool_t* pool);

// [RPC INTERNAL]
// Reserve at most `nfree' keytables with `key' pointing to data created by
// ctor(args).
extern void bthread_keytable_pool_reserve(
    bthread_keytable_pool_t* pool, size_t nfree,
    bthread_key_t key, void* ctor(const void* args), const void* args);

__END_DECLS

#endif  // BTHREAD_UNSTABLE_H
