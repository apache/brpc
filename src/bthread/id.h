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

#ifndef BTHREAD_ID_H
#define BTHREAD_ID_H

#include "butil/macros.h"              // BAIDU_SYMBOLSTR
#include "bthread/types.h"

__BEGIN_DECLS

// ----------------------------------------------------------------------
// Functions to create 64-bit identifiers that can be attached with data
// and locked without ABA issues. All functions can be called from
// multiple threads simultaneously. Notice that bthread_id_t is designed
// for managing a series of non-heavily-contended actions on an object.
// It's slower than mutex and not proper for general synchronizations.
// ----------------------------------------------------------------------

// Create a bthread_id_t and put it into *id. Crash when `id' is NULL.
// id->value will never be zero.
// `on_error' will be called after bthread_id_error() is called.
// -------------------------------------------------------------------------
// ! User must call bthread_id_unlock() or bthread_id_unlock_and_destroy()
// ! inside on_error.
// -------------------------------------------------------------------------
// Returns 0 on success, error code otherwise.
int bthread_id_create(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t id, void* data, int error_code)) __THROW;

// When this function is called successfully, *id, *id+1 ... *id + range - 1
// are mapped to same internal entity. Operations on any of the id work as
// if they're manipulating a same id. `on_error' is called with the id issued
// by corresponding bthread_id_error(). This is designed to let users encode
// versions into identifiers.
// `range' is limited inside [1, 1024].
int bthread_id_create_ranged(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t id, void* data, int error_code),
    int range) __THROW;

// Wait until `id' being destroyed.
// Waiting on a destroyed bthread_id_t returns immediately.
// Returns 0 on success, error code otherwise.
int bthread_id_join(bthread_id_t id) __THROW;

// Destroy a created but never-used bthread_id_t.
// Returns 0 on success, EINVAL otherwise.
int bthread_id_cancel(bthread_id_t id) __THROW;

// Issue an error to `id'.
// If `id' is not locked, lock the id and run `on_error' immediately. Otherwise
// `on_error' will be called with the error just before `id' being unlocked.
// If `id' is destroyed, un-called on_error are dropped.
// Returns 0 on success, error code otherwise.
#define bthread_id_error(id, err)                                        \
    bthread_id_error_verbose(id, err, __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__))

int bthread_id_error_verbose(bthread_id_t id, int error_code, 
                             const char *location) __THROW;

// Make other bthread_id_lock/bthread_id_trylock on the id fail, the id must
// already be locked. If the id is unlocked later rather than being destroyed,
// effect of this function is cancelled. This function avoids useless
// waiting on a bthread_id which will be destroyed soon but still needs to
// be joinable.
// Returns 0 on success, error code otherwise.
int bthread_id_about_to_destroy(bthread_id_t id) __THROW;

// Try to lock `id' (for using the data exclusively)
// On success return 0 and set `pdata' with the `data' parameter to
// bthread_id_create[_ranged], EBUSY on already locked, error code otherwise.
int bthread_id_trylock(bthread_id_t id, void** pdata) __THROW;

// Lock `id' (for using the data exclusively). If `id' is locked
// by others, wait until `id' is unlocked or destroyed.
// On success return 0 and set `pdata' with the `data' parameter to
// bthread_id_create[_ranged], error code otherwise.
#define bthread_id_lock(id, pdata)                                      \
    bthread_id_lock_verbose(id, pdata, __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__))
int bthread_id_lock_verbose(bthread_id_t id, void** pdata, 
                            const char *location) __THROW;

// Lock `id' (for using the data exclusively) and reset the range. If `id' is 
// locked by others, wait until `id' is unlocked or destroyed. if `range' if
// smaller than the original range of this id, nothing happens about the range
#define bthread_id_lock_and_reset_range(id, pdata, range)               \
    bthread_id_lock_and_reset_range_verbose(id, pdata, range,           \
                               __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__))
int bthread_id_lock_and_reset_range_verbose(
    bthread_id_t id, void **pdata, 
    int range, const char *location) __THROW;

// Unlock `id'. Must be called after a successful call to bthread_id_trylock()
// or bthread_id_lock().
// Returns 0 on success, error code otherwise.
int bthread_id_unlock(bthread_id_t id) __THROW;

// Unlock and destroy `id'. Waiters blocking on bthread_id_join() or
// bthread_id_lock() will wake up. Must be called after a successful call to
// bthread_id_trylock() or bthread_id_lock().
// Returns 0 on success, error code otherwise.
int bthread_id_unlock_and_destroy(bthread_id_t id) __THROW;

// **************************************************************************
// bthread_id_list_xxx functions are NOT thread-safe unless explicitly stated

// Initialize a list for storing bthread_id_t. When an id is destroyed, it will
// be removed from the list automatically.
// The commented parameters are not used anymore and just kept to not break
// compatibility.
int bthread_id_list_init(bthread_id_list_t* list,
                         unsigned /*size*/,
                         unsigned /*conflict_size*/) __THROW;
// Destroy the list.
void bthread_id_list_destroy(bthread_id_list_t* list) __THROW;

// Add a bthread_id_t into the list.
int bthread_id_list_add(bthread_id_list_t* list, bthread_id_t id) __THROW;

// Swap internal fields of two lists.
void bthread_id_list_swap(bthread_id_list_t* dest, 
                          bthread_id_list_t* src) __THROW;

// Issue error_code to all bthread_id_t inside `list' and clear `list'.
// Notice that this function iterates all id inside the list and may call
// on_error() of each valid id in-place, in another word, this thread-unsafe
// function is not suitable to be enclosed within a lock directly.
// To make the critical section small, swap the list inside the lock and
// reset the swapped list outside the lock as follows:
//   bthread_id_list_t tmplist;
//   bthread_id_list_init(&tmplist, 0, 0);
//   LOCK;
//   bthread_id_list_swap(&tmplist, &the_list_to_reset);
//   UNLOCK;
//   bthread_id_list_reset(&tmplist, error_code);
//   bthread_id_list_destroy(&tmplist);
int bthread_id_list_reset(bthread_id_list_t* list, int error_code) __THROW;
// Following 2 functions wrap above process.
int bthread_id_list_reset_pthreadsafe(
    bthread_id_list_t* list, int error_code, pthread_mutex_t* mutex) __THROW;
int bthread_id_list_reset_bthreadsafe(
    bthread_id_list_t* list, int error_code, bthread_mutex_t* mutex) __THROW;

__END_DECLS

#if defined(__cplusplus)
// cpp specific API, with an extra `error_text' so that error information
// is more comprehensive
int bthread_id_create2(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t id, void* data, int error_code,
                    const std::string& error_text)) __THROW;
int bthread_id_create2_ranged(
    bthread_id_t* id, void* data,
    int (*on_error)(bthread_id_t id, void* data, int error_code,
                    const std::string& error_text),
    int range) __THROW;
#define bthread_id_error2(id, ec, et)                                   \
    bthread_id_error2_verbose(id, ec, et, __FILE__ ":" BAIDU_SYMBOLSTR(__LINE__))
int bthread_id_error2_verbose(bthread_id_t id, int error_code,
                              const std::string& error_text,
                              const char *location) __THROW;
int bthread_id_list_reset2(bthread_id_list_t* list, int error_code,
                           const std::string& error_text) __THROW;
int bthread_id_list_reset2_pthreadsafe(bthread_id_list_t* list, int error_code,
                                       const std::string& error_text,
                                       pthread_mutex_t* mutex) __THROW;
int bthread_id_list_reset2_bthreadsafe(bthread_id_list_t* list, int error_code,
                                       const std::string& error_text,
                                       bthread_mutex_t* mutex) __THROW;
#endif

#endif  // BTHREAD_ID_H
