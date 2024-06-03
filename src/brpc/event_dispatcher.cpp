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


#include <gflags/gflags.h>                            // DEFINE_int32
#include "butil/compat.h"
#include "butil/fd_utility.h"                         // make_close_on_exec
#include "butil/logging.h"                            // LOG
#include "butil/third_party/murmurhash3/murmurhash3.h"// fmix32
#include "bthread/bthread.h"                          // bthread_start_background
#include "brpc/event_dispatcher.h"
#include "brpc/reloadable_flags.h"

DECLARE_int32(task_group_ntags);

namespace brpc {

DEFINE_int32(event_dispatcher_num, 1, "Number of event dispatcher");

DEFINE_bool(usercode_in_pthread, false, 
            "Call user's callback in pthreads, use bthreads otherwise");
DEFINE_bool(usercode_in_coroutine, false,
            "User's callback are run in coroutine, no bthread or pthread blocking call");

static EventDispatcher* g_edisp = NULL;
static pthread_once_t g_edisp_once = PTHREAD_ONCE_INIT;

static void StopAndJoinGlobalDispatchers() {
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        for (int j = 0; j < FLAGS_event_dispatcher_num; ++j) {
            g_edisp[i * FLAGS_event_dispatcher_num + j].Stop();
            g_edisp[i * FLAGS_event_dispatcher_num + j].Join();
        }
    }
}
void InitializeGlobalDispatchers() {
    g_edisp = new EventDispatcher[FLAGS_task_group_ntags * FLAGS_event_dispatcher_num];
    for (int i = 0; i < FLAGS_task_group_ntags; ++i) {
        for (int j = 0; j < FLAGS_event_dispatcher_num; ++j) {
            bthread_attr_t attr =
                FLAGS_usercode_in_pthread ? BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL;
            attr.tag = (BTHREAD_TAG_DEFAULT + i) % FLAGS_task_group_ntags;
            CHECK_EQ(0, g_edisp[i * FLAGS_event_dispatcher_num + j].Start(&attr));
        }
    }
    // This atexit is will be run before g_task_control.stop() because above
    // Start() initializes g_task_control by creating bthread (to run epoll/kqueue).
    CHECK_EQ(0, atexit(StopAndJoinGlobalDispatchers));
}

EventDispatcher& GetGlobalEventDispatcher(int fd, bthread_tag_t tag) {
    pthread_once(&g_edisp_once, InitializeGlobalDispatchers);
    if (FLAGS_task_group_ntags == 1 && FLAGS_event_dispatcher_num == 1) {
        return g_edisp[0];
    }
    int index = butil::fmix32(fd) % FLAGS_event_dispatcher_num;
    return g_edisp[tag * FLAGS_event_dispatcher_num + index];
}

int IOEventData::OnCreated(const IOEventDataOptions& options) {
    if (!options.input_cb) {
        LOG(ERROR) << "Invalid input_cb=NULL";
        return -1;
    }
    if (!options.output_cb) {
        LOG(ERROR) << "Invalid output_cb=NULL";
        return -1;
    }

    _options = options;
    return 0;
}

void IOEventData::BeforeRecycled() {
    _options = { NULL, NULL, NULL };
}

} // namespace brpc

#if defined(OS_LINUX)
    #include "brpc/event_dispatcher_epoll.cpp"
#elif defined(OS_MACOSX)
    #include "brpc/event_dispatcher_kqueue.cpp"
#else
    #error Not implemented
#endif
