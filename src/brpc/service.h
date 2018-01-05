// Copyright (c) 2015 Baidu, Inc.
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

// Authors: Kevin.XU (xuhuahai@sogou-inc.com)

#ifndef  BRPC_SERVICE_H
#define  BRPC_SERVICE_H

namespace brpc {

// For all kinds of business services
class BaseService {
public:
    virtual ~BaseService() {}

    // Check validation of service
    virtual bool checkValid() = 0;
};

} // namespace brpc


#endif  //BRPC_SERVICE_H
