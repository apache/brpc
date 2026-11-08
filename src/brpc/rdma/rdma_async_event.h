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

#ifndef BRPC_RDMA_ASYNC_EVENT_H
#define BRPC_RDMA_ASYNC_EVENT_H
#if BRPC_WITH_RDMA
#include <functional>
#include <infiniband/verbs.h>

namespace brpc {
namespace rdma {
// Prepares callbacks only for registered QPs, CQs and SRQs. Called under the
// registry lock: capture references or event values only. Do not wait for
// transport locks, destroy verbs objects or modify the registry. The captured
// callback runs after unlocking and acknowledging the event, so it may initiate
// destruction without waiting for its own event acknowledgement.
class RdmaAsyncEventContext {
public:
    virtual ~RdmaAsyncEventContext() = default;
    virtual std::function<void()> PrepareAsyncEvent(ibv_event_type type) = 0;
};
// Register after object creation and before publishing it to send/receive
// paths. The registry does not own the context.
void RegisterRdmaAsyncEventContext(const void* object, RdmaAsyncEventContext* context);
// Unregister before verbs destruction. Waits for any in-flight
// PrepareAsyncEvent call, but not for callbacks already captured. Destruction
// and its event-acknowledgement wait must run without the registry lock. Even
// if destruction fails, the object no longer receives new callbacks.
void UnregisterRdmaAsyncEventContext(const void* object);
bool DispatchRdmaAsyncEvent(ibv_async_event* event);
}  // namespace rdma
}  // namespace brpc
#endif
#endif  // BRPC_RDMA_ASYNC_EVENT_H
