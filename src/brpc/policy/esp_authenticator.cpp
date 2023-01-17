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

#include "butil/logging.h"
#include "butil/memory/singleton_on_pthread_once.h"
#include "brpc/policy/esp_authenticator.h"


namespace brpc {
namespace policy {

const char* MAGICNUM = "\0ESP\x01\x02";
const int MAGICNUM_LEN = 6;

int EspAuthenticator::GenerateCredential(std::string* auth_str) const {
    auth_str->assign(MAGICNUM, MAGICNUM_LEN);
    uint16_t local_port = 0;
    auth_str->append((char *)&local_port, sizeof(local_port));
    return 0;
}

int EspAuthenticator::VerifyCredential(
        const std::string& /*auth_str*/,
        const butil::EndPoint& /*client_addr*/,
        AuthContext* /*out_ctx*/) const {
    //nothing to do
    return 0;
}

const Authenticator* global_esp_authenticator() {
    return butil::get_leaky_singleton<EspAuthenticator>();
}

}  // namespace policy
} // namespace brpc
