// Copyright (c) 2017 Baidu, Inc.
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

// Authors: Zhangyi Chen (chenzhangyi01@baidu.com)

#ifndef  BRPC_HULU_PBRPC_CONTROLLER_H
#define  BRPC_HULU_PBRPC_CONTROLLER_H

#include <stdint.h>                             // int64_t
#include <string>                               // std::string
#include "brpc/controller.h"                    // Controller

namespace brpc {
namespace policy {

// Special Controller that can be filled with hulu-pbrpc specific meta fields.
class HuluController : public Controller {
public:
    HuluController()
        : _request_source_addr(0)
        , _response_source_addr(0)
    {}

    void Reset() {
        _request_source_addr = 0;
        _response_source_addr = 0;
        _request_user_data.clear();
        _response_user_data.clear();
        Controller::Reset();
    }


    // ------------------------------------------------------------------
    //                      Client-side methods
    // These calls shall be made from the client side only.  Their results
    // are undefined on the server side (may crash).
    // ------------------------------------------------------------------
   
    // Send the address that the client listens as a server to the remote
    // side.
    int64_t request_source_addr() const { return _request_source_addr; }
    void set_request_source_addr(int64_t request_source_addr)
    { _request_source_addr = request_source_addr; }

    // Send a raw data along with the Hulu rpc meta instead of carrying it with
    // the request.
    const std::string& request_user_data() const { return _request_user_data; }
    void set_request_user_data(const std::string& request_user_data)
    { _request_user_data = request_user_data; }

    // ------------------------------------------------------------------------
    //                      Server-side methods.
    // These calls shall be made from the server side only. Their results are
    // undefined on the client side (may crash).
    // ------------------------------------------------------------------------
    
    // Send the address that the server listens to the remote side.
    int64_t response_source_addr() const { return _response_source_addr; }
    void set_response_source_addr(int64_t response_source_addr)
    { _response_source_addr = response_source_addr; }

    // Send a raw data along with the Hulu rpc meta instead of carrying it with
    // the response.
    const std::string& response_user_data() const { return _response_user_data; }
    void set_response_user_data(const std::string& response_user_data)
    { _response_user_data = response_user_data; }
    

private:
    int64_t _request_source_addr;
    int64_t _response_source_addr;
    std::string _request_user_data;
    std::string _response_user_data;
};

}  // namespace policy
} // namespace brpc

#endif  //BRPC_HULU_PBRPC_CONTROLLER_H
