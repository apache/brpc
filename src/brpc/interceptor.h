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

#ifndef BRPC_INTERCEPTOR_H
#define BRPC_INTERCEPTOR_H

#include "brpc/controller.h"


namespace brpc {

class Interceptor {
public:
    virtual ~Interceptor() = default;

    // Returns true if accept request, reject request otherwise.
    // When server rejects request, You can fill `error_code'
    // and `error_txt' which will send to client.
    virtual bool Accept(const brpc::Controller* controller,
                        int& error_code,
                        std::string& error_txt) const = 0;

};

} // namespace brpc


#endif //BRPC_INTERCEPTOR_H
