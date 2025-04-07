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

#include "ring_module.h"
#include "ring_listener.h"
#include <atomic>

#ifdef IO_URING_ENABLED
void RingModule::ExtThdStart(int thd_id) {
    listeners_.at(thd_id)->has_external_.store(true, std::memory_order_relaxed);
}

void RingModule::ExtThdEnd(int thd_id) { listeners_.at(thd_id)->ExtWakeup(); }

void RingModule::Process(int thd_id) {
    listeners_.at(thd_id)->ExtPoll();
}

bool RingModule::HasTask(int thd_id) const {
    RingListener *listener = listeners_.at(thd_id);
    return listener->HasTasks();
}

void RingModule::AddListener(int thd_id, RingListener *listener) {
    // protected by TaskControl::_modify_group_mutex
    listeners_[thd_id] = listener;
}

#endif
