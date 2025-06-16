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

#include "eloq_module.h"
#include "bthread/bthread.h"

extern std::array<eloq::EloqModule *, 10> registered_modules;
extern std::atomic<int> registered_module_cnt;

namespace eloq {
    bool EloqModule::NotifyWorker(int thd_id) {
        return bthread_notify_worker(thd_id);
    }

    int register_module(EloqModule *module) {
        static std::mutex module_mutex;
        std::unique_lock<std::mutex> lk(module_mutex);
        size_t i = 0;
        while (i < registered_modules.size() && registered_modules[i] != nullptr) {
            // Each module should only be registered once.
            CHECK(registered_modules[i] != module);
            i++;
        }
        registered_modules[i] = module;
        registered_module_cnt.fetch_add(1, std::memory_order_release);
        return 0;
    }

    int unregister_module(EloqModule *module) {
        static std::mutex module_mutex;
        std::unique_lock<std::mutex> lk(module_mutex);
        size_t i = 0;
        while (i < registered_modules.size() && registered_modules[i] != module) {
            i++;
        }
        if (i == registered_modules.size()) {
            return 0;
        }
        CHECK(i < registered_module_cnt);
        while (i < registered_modules.size() - 1) {
            registered_modules[i] = registered_modules[i + 1];
            i++;
        }
        registered_module_cnt.fetch_sub(1, std::memory_order_release);
        return 0;
    }
} // namespace eloq
