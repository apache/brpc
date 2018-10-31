// Copyright (c) 2018 Iqiyi, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Cai,Daojin (caidaojin@qiyi.com)

#include "brpc/couchbase_retry_policy.h"
#include "brpc/controller.h"
#include "brpc/couchbase_helper.h"
#include "brpc/couchbase.h"
#include "brpc/policy/couchbase_load_balancer.h"
#include "butil/strings/string_number_conversions.h" 
#include "butil/string_printf.h"

namespace brpc {

namespace policy {

// Serialize a memcache request.
void SerializeMemcacheRequest(butil::IOBuf* buf, Controller* cntl,
                              const google::protobuf::Message* request);

}

static inline std::string BuildErrorText(
    const CouchbaseResponse::Status status,
    const int vb_id, const std::string& remote_side) {
    std::string error_text(CouchbaseResponse::status_str(status));
    butil::string_appendf(&error_text, "(vbucket_id=%d) @%s;",
                          vb_id, remote_side.c_str());
    return std::move(error_text);
}

bool CouchbaseRetryPolicy::DoRetry(Controller* cntl) const {
    const int error_code = cntl->ErrorCode();
    uint32_t reason = DEFAULT_DUMMY;
    CouchbaseResponse::Status status = CouchbaseResponse::STATUS_SUCCESS;
    if (error_code != 0) {
        if (error_code == EHOSTDOWN || error_code == ELOGOFF || 
            error_code == EFAILEDSOCKET || error_code == EEOF || 
            error_code == ECLOSE || error_code == ECONNRESET) {
            reason = SERVER_DOWN;
        } else {
            reason = RPC_FAILED;
        }
    } else {
        if (!cntl->has_request_code()) {
            return false;
        }
        CouchbaseResponse* response = 
            static_cast<CouchbaseResponse*>(cntl->response());
        if (response->GetStatus(&status)) {
            if (status != CouchbaseResponse::STATUS_SUCCESS) { 
                reason = status == CouchbaseResponse::STATUS_NOT_MY_VBUCKET
                         ? RPC_SUCCESS_BUT_WRONG_SERVER 
                         : RPC_SUCCESS_BUT_RESPONSE_FAULT;
             } else {
                 reason = RESPONSE_OK;
             }
        }
    }
    bool rebalance = false;
    size_t server_num = 0;
    if (!CouchbaseHelper::GetVBucketMapInfo(cntl->_lb, &server_num,
                                            nullptr, &rebalance)) {
        return false;
    }
    uint32_t vb_id, pre_reason;
    CouchbaseHelper::ParseRequestCode(cntl->request_code(), &vb_id, &pre_reason);
    const std::string curr_server = butil::endpoint2str(cntl->remote_side()).c_str();
    if (rebalance) {
        CouchbaseHelper::UpdateDetectedMasterIfNeeded(
            cntl->_lb, pre_reason == SERVER_DOWN_RETRY_REPLICAS,
            vb_id, reason, curr_server);
    }
    if (reason == RPC_FAILED || reason == RESPONSE_OK) {
        return false;
    }
    if (status != CouchbaseResponse::STATUS_SUCCESS) {
        std::string text = BuildErrorText(status, vb_id, curr_server);
        reinterpret_cast<CouchbaseResponse*>(cntl->response())->_retry_err.append(text);
    }
    cntl->set_max_retry(server_num - 1);
    bool ret = false;
    if (rebalance) {
        ret = reason == SERVER_DOWN || 
              reason == RPC_SUCCESS_BUT_WRONG_SERVER || 
              reason == RPC_SUCCESS_BUT_RESPONSE_FAULT; 
    } else if(!brpc::FLAGS_couchbase_disable_retry_during_active) {
        if ((reason == SERVER_DOWN && !cntl->couchbase_key_read_replicas().empty()) 
            || pre_reason == SERVER_DOWN_RETRY_REPLICAS) {
            reason = SERVER_DOWN_RETRY_REPLICAS;    
        }
        ret = reason == RPC_SUCCESS_BUT_WRONG_SERVER || 
              reason == SERVER_DOWN_RETRY_REPLICAS; 
    }
    if (ret) {
        // CouchbaseLoadBalancer will select server according to the retry reason.
        uint64_t req_code = CouchbaseHelper::AddReasonToRequestCode(
            cntl->request_code(), reason);
        cntl->set_request_code(req_code);
        // Read replica server, we need re-package request.
        if (reason == SERVER_DOWN_RETRY_REPLICAS) {
            CouchbaseRequest request;
            request.ReplicasGet(cntl->couchbase_key_read_replicas(), vb_id);
            cntl->_error_code = 0;
            policy::SerializeMemcacheRequest(&cntl->_request_buf, cntl, &request);
            if (cntl->FailedInline()) {
                return false;
            }
        }
    }
    return ret;
}

} // namespace brpc
