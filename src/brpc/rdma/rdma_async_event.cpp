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

#if BRPC_WITH_RDMA
#include "brpc/rdma/rdma_async_event.h"
#include <mutex>
#include "bthread/mutex.h"
#include "butil/containers/flat_map.h"
#include "butil/logging.h"

namespace brpc {
namespace rdma {
extern void (*IbvAckAsyncEvent)(ibv_async_event*);

namespace {
struct AsyncEventRegistry {
    // Serializes registration, removal and callback capture only. Event
    // acknowledgement, verbs destruction and callback execution run outside
    // this lock.
    bthread::Mutex mutex;
    // Look up verbs objects by identity. External objects may contain arbitrary
    // context pointers that must not be dereferenced.
    butil::FlatMap<const void*, RdmaAsyncEventContext*> contexts;
};

AsyncEventRegistry& Registry() {
    // RDMA atexit cleanup still unregisters objects, so the registry must
    // outlive static destruction.
    static auto* registry = new AsyncEventRegistry;
    return *registry;
}
}  // namespace

void RegisterRdmaAsyncEventContext(const void* object,
                                   RdmaAsyncEventContext* context) {
    CHECK(object);
    CHECK(context);
    auto* registry = &Registry();
    std::lock_guard<bthread::Mutex> lock(registry->mutex);
    CHECK(registry->contexts.seek(object) == nullptr);
    registry->contexts[object] = context;
}

void UnregisterRdmaAsyncEventContext(const void* object) {
    auto* registry = &Registry();
    std::lock_guard<bthread::Mutex> lock(registry->mutex);
    registry->contexts.erase(object);
}

bool DispatchRdmaAsyncEvent(ibv_async_event* event) {
    const void* object = nullptr;
    switch (event->event_type) {
    case IBV_EVENT_QP_REQ_ERR:
    case IBV_EVENT_QP_ACCESS_ERR:
    case IBV_EVENT_QP_FATAL:
    case IBV_EVENT_QP_LAST_WQE_REACHED:
    case IBV_EVENT_COMM_EST:
    case IBV_EVENT_SQ_DRAINED:
        object = event->element.qp;
        break;
    case IBV_EVENT_CQ_ERR:
        object = event->element.cq;
        break;
    case IBV_EVENT_SRQ_ERR:
    case IBV_EVENT_SRQ_LIMIT_REACHED:
        object = event->element.srq;
        break;
    default:
        return false;
    }
    std::function<void()> callback;
    {
        auto* registry = &Registry();
        std::lock_guard<bthread::Mutex> lock(registry->mutex);
        auto* context = registry->contexts.seek(object);
        if (context != nullptr) {
            callback = (*context)->PrepareAsyncEvent(event->event_type);
        }
    }
    // Acknowledge events for unregistered objects too, without accessing their
    // context. A callback may destroy its object. Acknowledge first so
    // destruction cannot wait for this callback to acknowledge its own event.
    IbvAckAsyncEvent(event);
    if (callback) {
        callback();
    }
    return true;
}
}  // namespace rdma
}  // namespace brpc
#endif  // BRPC_WITH_RDMA
