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

#ifndef BRPC_DETAILS_SERVER_TLS_CONTEXT_H
#define BRPC_DETAILS_SERVER_TLS_CONTEXT_H

#include <stdint.h>
#include <time.h>

#include "butil/atomicops.h"
#include "butil/macros.h"
#include "butil/memory/ref_counted.h"
#include "butil/synchronization/lock.h"

namespace brpc {
namespace details {

// Tracks admission and draining for one Server TLS generation. This component
// does not authorize TLS access, represent request execution scope, stop
// listeners, or perform resource teardown. Metadata pins protect this object's
// storage only. The owner must separately coordinate Server lifecycle and hold
// exclusive cleanup ownership before calling Retire().
class ServerTLSContext
    : public butil::RefCountedThreadSafe<ServerTLSContext> {
public:
    enum class Phase {
        INITIALIZING,
        OPEN,
        DRAINING,
        SEALED,
        RETIRED
    };

    enum class LeaseKind {
        TASK,
        PRODUCER,
        REQUEST
    };

    struct Snapshot {
        Phase phase;
        uint64_t lease_count;
        bool startup_committed;
    };

    class Lease {
    public:
        Lease();
        ~Lease();
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;

        // One Lease object has one owner. Do not call Reset(), Derive(), or a
        // move operation concurrently on the same instance. Transfer a lease
        // between executors by moving it after the previous owner stops using
        // it; copying a context pointer does not transfer admission authority.
        bool valid() const { return _context != nullptr; }
        void Reset();

        // output must point to an empty Lease. A REQUEST lease may derive only
        // a TASK continuation; TASK/PRODUCER leases may derive TASK/PRODUCER.
        int Derive(LeaseKind kind, Lease* output) const;

    private:
        friend class ServerTLSContext;

        Lease(const scoped_refptr<ServerTLSContext>& context,
              LeaseKind kind, bool bootstrap_task);

        scoped_refptr<ServerTLSContext> _context;
        LeaseKind _kind;
        bool _bootstrap_task;

        DISALLOW_COPY_AND_ASSIGN(Lease);
    };

    ServerTLSContext();

    Snapshot GetSnapshot() const;

    // Admission output must point to an empty Lease and is populated only on
    // success. These operations do not grant the caller direct TLS access.
    int AdmitBootstrap(LeaseKind kind, Lease* output);
    int AdmitIndependent(LeaseKind kind, Lease* output);
    int CommitStartup();
    void BeginDrain();
    int WaitSealed(const timespec* abstime = nullptr);

    // The caller must already hold exclusive cleanup ownership, have waited
    // for listeners, and have completed cache destruction and key retirement.
    int Retire();

private:
    friend class butil::RefCountedThreadSafe<ServerTLSContext>;

    ~ServerTLSContext();

    static bool IsValidKind(LeaseKind kind);
    int DeriveLease(LeaseKind source_kind, LeaseKind requested_kind,
                    Lease* output);
    int AddLeaseLocked(bool bootstrap_task);
    void ReleaseLease(bool bootstrap_task);
    void PublishSealedLocked();

    mutable butil::Mutex _mutex;
    Phase _phase;
    uint64_t _lease_count;
    uint64_t _bootstrap_task_count;
    bool _startup_committed;
    butil::atomic<int>* _wake_sequence;

    DISALLOW_COPY_AND_ASSIGN(ServerTLSContext);
};

}  // namespace details
}  // namespace brpc

#endif  // BRPC_DETAILS_SERVER_TLS_CONTEXT_H
