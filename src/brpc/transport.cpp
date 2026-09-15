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

#include "brpc/transport.h"

namespace brpc {
DECLARE_bool(usercode_in_coroutine);
DECLARE_bool(usercode_in_pthread);

void Transport::QueueInputMessageBatch(InputMessageBatch* input_msgs,
                                       int* num_bthread_created,
                                       bool run_inline) {
    if (input_msgs == nullptr || input_msgs->empty()) {
        delete input_msgs;
        return;
    }
    if (run_inline || FLAGS_usercode_in_coroutine) {
        ProcessInputMessageBatch(input_msgs);
        return;
    }

    bthread_t th;
    bthread_attr_t tmp =
        (FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL) |
        BTHREAD_NOSIGNAL;
    tmp.keytable_pool = _socket->keytable_pool();
    tmp.tag = bthread_self_tag();
    bthread_attr_set_name(&tmp, "ProcessInputMessageBatch");
    if (bthread_start_background(
            &th, &tmp, ProcessInputMessageBatch, input_msgs) == 0) {
        ++*num_bthread_created;
    } else {
        ProcessInputMessageBatch(input_msgs);
    }
}

}  // namespace brpc
