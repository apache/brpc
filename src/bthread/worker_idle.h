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

#ifndef BTHREAD_WORKER_IDLE_H
#define BTHREAD_WORKER_IDLE_H

#include <stdint.h>
#include <time.h>

namespace bthread {

// Registers a per-worker init function and an idle function.
//
// The init function is called at most once per worker thread, before running
// the idle function in that worker thread.
//
// Args:
//   init_fn: Optional. Can be NULL.
//   idle_fn: Required. Must not be NULL.
//   timeout_us: Required. Must be > 0.
//   handle: Optional output handle for unregistering later.
//
// Returns:
//   0 on success, error code otherwise.
int register_worker_idle_function(int (*init_fn)(void),
                                  bool (*idle_fn)(void),
                                  uint64_t timeout_us,
                                  int* handle);

// Unregisters a previously registered idle function by handle.
//
// Args:
//   handle: Handle returned by register_worker_idle_function().
//
// Returns:
//   0 on success, error code otherwise.
int unregister_worker_idle_function(int handle);

// Returns true if any idle function is registered.
bool has_worker_idle_functions();

// Runs all registered idle functions for current worker thread.
void run_worker_idle_functions();

// Get the minimal timeout among all registered functions.
// Returns {0,0} if no idle function is registered.
timespec get_worker_idle_timeout();

}  // namespace bthread

#endif  // BTHREAD_WORKER_IDLE_H


