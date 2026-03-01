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


#ifndef BRPC_BACKUP_REQUEST_POLICY_H
#define BRPC_BACKUP_REQUEST_POLICY_H

#include "brpc/controller.h"

namespace brpc {

class BackupRequestPolicy {
public:
    virtual ~BackupRequestPolicy() = default;

    // Return the time in milliseconds in which another request
    // will be sent if RPC does not finish.
    virtual int32_t GetBackupRequestMs(const Controller* controller) const = 0;

    // Return true if the backup request should be sent.
    virtual bool DoBackup(const Controller* controller) const = 0;

    // Called  when a rpc is end, user can collect call information to adjust policy.
    virtual void OnRPCEnd(const Controller* controller) = 0;
};

// Options for CreateRateLimitedBackupPolicy().
// All fields have defaults matching the recommended starting values.
struct RateLimitedBackupPolicyOptions {
    // Time in milliseconds after which a backup request is sent if the RPC
    // has not completed. Must be >= 0.
    // Default: 0
    int32_t backup_request_ms = 0;

    // Maximum ratio of backup requests to total requests in the sliding
    // window. Must be in (0, 1].
    // Default: 0.1
    double max_backup_ratio = 0.1;

    // Width of the sliding time window in seconds. Must be in [1, 3600].
    // Default: 10
    int window_size_seconds = 10;

    // Interval in seconds between cached-ratio refreshes. Must be >= 1.
    // Default: 5
    int update_interval_seconds = 5;
};

// Create a BackupRequestPolicy that limits the ratio of backup requests
// to total requests within a sliding time window. When the ratio reaches
// or exceeds options.max_backup_ratio, DoBackup() returns false.
// NOTE: Backup decisions are counted immediately at DoBackup() time for
// fast feedback. Total RPCs are counted on completion (OnRPCEnd). During
// latency spikes the ratio may temporarily lag until RPCs complete.
// Returns NULL on invalid parameters.
// The caller owns the returned pointer.
BackupRequestPolicy* CreateRateLimitedBackupPolicy(
    const RateLimitedBackupPolicyOptions& options);

}

#endif // BRPC_BACKUP_REQUEST_POLICY_H
