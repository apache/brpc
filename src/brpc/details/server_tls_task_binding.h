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

#ifndef BRPC_DETAILS_SERVER_TLS_TASK_BINDING_H
#define BRPC_DETAILS_SERVER_TLS_TASK_BINDING_H

#include "bthread/types.h"
#include "butil/memory/ref_counted.h"

namespace brpc {
namespace details {

class ServerTLSContext;

// Registers a retained pool header as one managed Server TLS generation.
// The pool must already be initialized, must not yet be exposed through an
// attribute or task, and must never be freed or reused for another generation.
// Registration is intended for trusted Server lifecycle code serialized with
// startup and drain; it does not itself prove startup authority.
//
// This task-only binding authorizes TaskGroup admission. It is not the full
// key/assigned-data registry and does not authorize direct Server TLS access.
int RegisterServerTLSTaskPool(
    bthread_keytable_pool_t* pool,
    const scoped_refptr<ServerTLSContext>& context);

// Converts a registered binding into a permanent tombstone. The context must
// already be RETIRED. Tombstones are deliberately retained so stale copied
// attributes fail closed instead of falling back to legacy pool behavior.
int RetireServerTLSTaskPool(bthread_keytable_pool_t* pool);

}  // namespace details
}  // namespace brpc

#endif  // BRPC_DETAILS_SERVER_TLS_TASK_BINDING_H
