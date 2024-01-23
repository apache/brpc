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

#include "butil/time.h"
#include "butil/logging.h"
#include "brpc/controller.h"           // Controller
#include "brpc/closure_guard.h"        // ClosureGuard
#include "brpc/builtin/memory_service.h"
#include "brpc/details/tcmalloc_extension.h"

namespace brpc {

DEFINE_int32(max_tc_stats_buf_len, 32 * 1024, "max length of TCMalloc stats");
BRPC_VALIDATE_GFLAG(max_tc_stats_buf_len, PositiveInteger);

static inline void get_tcmalloc_num_prop(MallocExtension* malloc_ext,
                                         const char* prop_name,
                                         butil::IOBufBuilder& os) {
    size_t value;
    if (malloc_ext->GetNumericProperty(prop_name, &value)) {
        os << prop_name << ": " << value << "\n";
    }
}

static void get_tcmalloc_memory_info(butil::IOBuf& out) {
    MallocExtension* malloc_ext = MallocExtension::instance();
    butil::IOBufBuilder os;
    os << "------------------------------------------------\n";
    get_tcmalloc_num_prop(malloc_ext, "generic.total_physical_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "generic.current_allocated_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "generic.heap_size", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.current_total_thread_cache_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.central_cache_free_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.transfer_cache_free_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.thread_cache_free_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.pageheap_free_bytes", os);
    get_tcmalloc_num_prop(malloc_ext, "tcmalloc.pageheap_unmapped_bytes", os);

    int32_t len = FLAGS_max_tc_stats_buf_len;
    std::unique_ptr<char[]> buf(new char[len]);
    malloc_ext->GetStats(buf.get(), len);
    os << buf.get();

    os.move_to(out);
}

void MemoryService::default_method(::google::protobuf::RpcController* cntl_base,
                                    const ::brpc::MemoryRequest*,
                                    ::brpc::MemoryResponse*,
                                    ::google::protobuf::Closure* done) {
    ClosureGuard done_guard(done);
    auto cntl = static_cast<Controller*>(cntl_base);
    cntl->http_response().set_content_type("text/plain");
    butil::IOBuf& resp = cntl->response_attachment();

    if (IsTCMallocEnabled()) {
        butil::IOBufBuilder os;
        get_tcmalloc_memory_info(resp);
    } else {
        resp.append("tcmalloc is not enabled");
        cntl->http_response().set_status_code(HTTP_STATUS_FORBIDDEN);
        return;
    }
}

} // namespace brpc
