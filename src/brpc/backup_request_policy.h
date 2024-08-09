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
    virtual int32_t GetBackupRequestMs() const = 0;

    // Return true if the backup request should be sent.
    virtual bool DoBackup(const Controller* controller) const = 0;
};

}

#endif // BRPC_BACKUP_REQUEST_POLICY_H
