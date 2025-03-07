/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once
#include "bthread/brpc_module.h"
#include "bthread/types.h"
#include <array>
#include <thread>

#ifdef IO_URING_ENABLED
class RingListener;

class RingModule : public eloq::EloqModule {
public:
    void ExtThdStart(int thd_id) override;

    void ExtThdEnd(int thd_id) override;

    void Process(int thd_id) override;

    bool HasTask(int thd_id) const override;

    void AddListener(int thd_id, RingListener *listener);

private:
    std::array<RingListener *, BTHREAD_MAX_CONCURRENCY> listeners_{};
};

#endif
