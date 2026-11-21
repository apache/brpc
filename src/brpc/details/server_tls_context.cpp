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

#include "brpc/details/server_tls_context.h"

#include <errno.h>
#include <limits>
#include <utility>

#include "bthread/butex.h"
#include "butil/scoped_lock.h"

namespace brpc {
namespace details {

ServerTLSContext::Lease::Lease()
    : _kind(LeaseKind::TASK)
    , _bootstrap_task(false) {
}

ServerTLSContext::Lease::Lease(
        const scoped_refptr<ServerTLSContext>& context,
        LeaseKind kind, bool bootstrap_task)
    : _context(context)
    , _kind(kind)
    , _bootstrap_task(bootstrap_task) {
}

ServerTLSContext::Lease::~Lease() {
    Reset();
}

ServerTLSContext::Lease::Lease(Lease&& other) noexcept
    : _context(std::move(other._context))
    , _kind(other._kind)
    , _bootstrap_task(other._bootstrap_task) {
    other._bootstrap_task = false;
}

ServerTLSContext::Lease& ServerTLSContext::Lease::operator=(
        Lease&& other) noexcept {
    if (this != &other) {
        Reset();
        _context.swap(other._context);
        _kind = other._kind;
        _bootstrap_task = other._bootstrap_task;
        other._bootstrap_task = false;
    }
    return *this;
}

void ServerTLSContext::Lease::Reset() {
    scoped_refptr<ServerTLSContext> context;
    context.swap(_context);
    if (context != nullptr) {
        const bool bootstrap_task = _bootstrap_task;
        _bootstrap_task = false;
        context->ReleaseLease(bootstrap_task);
    }
}

int ServerTLSContext::Lease::Derive(LeaseKind kind, Lease* output) const {
    if (_context == nullptr) {
        return EINVAL;
    }
    return _context->DeriveLease(_kind, kind, output);
}

ServerTLSContext::ServerTLSContext()
    : _phase(Phase::INITIALIZING)
    , _lease_count(0)
    , _bootstrap_task_count(0)
    , _startup_committed(false)
    , _wake_sequence(
          bthread::butex_create_checked<butil::atomic<int> >()) {
    _wake_sequence->store(0, butil::memory_order_relaxed);
}

ServerTLSContext::~ServerTLSContext() {
    bthread::butex_destroy(_wake_sequence);
}

bool ServerTLSContext::IsValidKind(LeaseKind kind) {
    return kind == LeaseKind::TASK || kind == LeaseKind::PRODUCER || kind == LeaseKind::REQUEST;
}

ServerTLSContext::Snapshot ServerTLSContext::GetSnapshot() const {
    BAIDU_SCOPED_LOCK(_mutex);
    const Snapshot result = {_phase, _lease_count, _startup_committed};
    return result;
}

int ServerTLSContext::AddLeaseLocked(bool bootstrap_task) {
    if (_lease_count == std::numeric_limits<uint64_t>::max()) {
        return EOVERFLOW;
    }
    ++_lease_count;
    if (bootstrap_task) {
        ++_bootstrap_task_count;
    }
    return 0;
}

int ServerTLSContext::AdmitBootstrap(LeaseKind kind, Lease* output) {
    if (!IsValidKind(kind) || kind == LeaseKind::REQUEST || output == nullptr ||
        output->valid()) {
        return EINVAL;
    }
    scoped_refptr<ServerTLSContext> pin(this);
    const bool bootstrap_task = kind == LeaseKind::TASK;
    int rc = 0;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        if (_phase != Phase::INITIALIZING) {
            return ECANCELED;
        }
        rc = AddLeaseLocked(bootstrap_task);
    }
    if (rc == 0) {
        *output = Lease(pin, kind, bootstrap_task);
    }
    return rc;
}

int ServerTLSContext::AdmitIndependent(LeaseKind kind, Lease* output) {
    if (!IsValidKind(kind) || output == nullptr || output->valid()) {
        return EINVAL;
    }
    scoped_refptr<ServerTLSContext> pin(this);
    int rc = 0;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        if (_phase != Phase::OPEN) {
            return ECANCELED;
        }
        rc = AddLeaseLocked(false);
    }
    if (rc == 0) {
        *output = Lease(pin, kind, false);
    }
    return rc;
}

int ServerTLSContext::DeriveLease(LeaseKind source_kind,
                                  LeaseKind requested_kind,
                                  Lease* output) {
    if (!IsValidKind(source_kind) || !IsValidKind(requested_kind) ||
        requested_kind == LeaseKind::REQUEST || output == nullptr || output->valid()) {
        return EINVAL;
    }
    if (source_kind == LeaseKind::REQUEST && requested_kind != LeaseKind::TASK) {
        return EINVAL;
    }
    scoped_refptr<ServerTLSContext> pin(this);
    int rc = 0;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        if (!_startup_committed ||
            (_phase != Phase::OPEN && _phase != Phase::DRAINING)) {
            return ECANCELED;
        }
        rc = AddLeaseLocked(false);
    }
    if (rc == 0) {
        *output = Lease(pin, requested_kind, false);
    }
    return rc;
}

int ServerTLSContext::CommitStartup() {
    BAIDU_SCOPED_LOCK(_mutex);
    if (_phase == Phase::OPEN) {
        return EALREADY;
    }
    if (_phase != Phase::INITIALIZING) {
        return ECANCELED;
    }
    if (_bootstrap_task_count != 0) {
        return EBUSY;
    }
    _startup_committed = true;
    _phase = Phase::OPEN;
    return 0;
}

void ServerTLSContext::PublishSealedLocked() {
    _phase = Phase::SEALED;
    const int current =
        _wake_sequence->load(butil::memory_order_relaxed);
    const int next = current == std::numeric_limits<int>::max()
                         ? 0
                         : current + 1;
    _wake_sequence->store(next, butil::memory_order_release);
}

void ServerTLSContext::BeginDrain() {
    scoped_refptr<ServerTLSContext> pin(this);
    bool wake = false;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        if (_phase != Phase::INITIALIZING && _phase != Phase::OPEN) {
            return;
        }
        if (_lease_count == 0) {
            PublishSealedLocked();
            wake = true;
        } else {
            _phase = Phase::DRAINING;
        }
    }
    if (wake) {
        // Task exit may publish SEALED after publishing task completion. This
        // wake variant never exchanges to a waiter on the releasing stack.
        bthread::butex_wake_except(_wake_sequence, INVALID_BTHREAD);
    }
}

void ServerTLSContext::ReleaseLease(bool bootstrap_task) {
    bool wake = false;
    {
        BAIDU_SCOPED_LOCK(_mutex);
        CHECK_GT(_lease_count, 0UL);
        --_lease_count;
        if (bootstrap_task) {
            CHECK_GT(_bootstrap_task_count, 0UL);
            --_bootstrap_task_count;
        }
        if (_phase == Phase::DRAINING && _lease_count == 0) {
            PublishSealedLocked();
            wake = true;
        }
    }
    if (wake) {
        bthread::butex_wake_except(_wake_sequence, INVALID_BTHREAD);
    }
}

int ServerTLSContext::WaitSealed(const timespec* abstime) {
    scoped_refptr<ServerTLSContext> pin(this);
    for (;;) {
        int expected = 0;
        {
            BAIDU_SCOPED_LOCK(_mutex);
            if (_phase == Phase::SEALED || _phase == Phase::RETIRED) {
                return 0;
            }
            expected =
                _wake_sequence->load(butil::memory_order_relaxed);
        }
        if (bthread::butex_wait(_wake_sequence, expected, abstime) == 0) {
            continue;
        }
        if (errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        const int saved_errno = errno;
        {
            BAIDU_SCOPED_LOCK(_mutex);
            if (_phase == Phase::SEALED || _phase == Phase::RETIRED) {
                return 0;
            }
        }
        return saved_errno;
    }
}

int ServerTLSContext::Retire() {
    BAIDU_SCOPED_LOCK(_mutex);
    if (_phase == Phase::RETIRED) {
        return 0;
    }
    if (_phase != Phase::SEALED) {
        return EBUSY;
    }
    _phase = Phase::RETIRED;
    return 0;
}

}  // namespace details
}  // namespace brpc
