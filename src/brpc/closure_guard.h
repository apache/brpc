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


#ifndef BRPC_CLOSURE_GUARD_H
#define BRPC_CLOSURE_GUARD_H

#include <google/protobuf/service.h>
#include "butil/macros.h"


namespace brpc {

// RAII: Call Run() of the closure on destruction.
class ClosureGuard {
public:
    ClosureGuard() : _done(NULL) {}

    // Constructed with a closure which will be Run() inside dtor.
    explicit ClosureGuard(google::protobuf::Closure* done) : _done(done) {}
    
    // Run internal closure if it's not NULL.
    ~ClosureGuard() {
        if (_done) {
            _done->Run();
        }
    }

    // Run internal closure if it's not NULL and set it to `done'.
    void reset(google::protobuf::Closure* done) {
        if (_done) {
            _done->Run();
        }
        _done = done;
    }

    // Return and set internal closure to NULL.
    google::protobuf::Closure* release() {
        google::protobuf::Closure* const prev_done = _done;
        _done = NULL;
        return prev_done;
    }

    // True if no closure inside.
    bool empty() const { return _done == NULL; }

    // Exchange closure with another guard.
    void swap(ClosureGuard& other) { std::swap(_done, other._done); }
    
private:
    // Copying this object makes no sense.
    DISALLOW_COPY_AND_ASSIGN(ClosureGuard);
    
    google::protobuf::Closure* _done;
};

} // namespace brpc


#endif  // BRPC_CLOSURE_GUARD_H
