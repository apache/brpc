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

#ifndef BRPC_UB_HELPER_H
#define BRPC_UB_HELPER_H

#if BRPC_WITH_UBRING

#include <string>
#include <functional>
#include "bthread/types.h"


namespace brpc {
    namespace ubring {

        void GlobalRelease();

        void GlobalUBInitializeOrDie();

        bool InitPollingModeWithTag(bthread_tag_t tag,
                                    std::function<void(void)> callback = nullptr,
                                    std::function<void(void)> init_fn = nullptr,
                                    std::function<void(void)> release_fn = nullptr);

        // If the UB environment is available
        bool IsUBAvailable();

        // Disable UB in the remaining lifetime of the process
        void GlobalDisableUb();

        // If the given protocol supported by UB
        bool SupportedByUB(std::string protocol);

    }  // namespace ubring
}  // namespace brpc
#else
namespace brpc {
    namespace ubring {

        void GlobalRelease();

        // Initialize UB environment
        // Exit if failed
        void GlobalUBInitializeOrDie();

    }  // namespace ubring
}  // namespace brpc
#endif  // if BRPC_WITH_UBRING

#endif //BRPC_UB_HELPER_H
