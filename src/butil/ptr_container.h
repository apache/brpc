// Copyright (c) 2018 brpc authors.
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

// Author: Ge,Jun (jge666@gmail.com)
// Date: Fri Sep  7 12:15:23 CST 2018

#ifndef BUTIL_PTR_CONTAINER_H
#define BUTIL_PTR_CONTAINER_H

namespace butil {

// Manage lifetime of a pointer. The key difference between PtrContainer and
// unique_ptr is that PtrContainer can be copied and the pointer inside is
// deeply copied or constructed on-demand.
template <typename T>
class PtrContainer {
public:
    PtrContainer() : _ptr(NULL) {}

    explicit PtrContainer(T* obj) : _ptr(obj) {}

    ~PtrContainer() {
        delete _ptr;
    }

    PtrContainer(const PtrContainer& rhs)
        : _ptr(rhs._ptr ? new T(*rhs._ptr) : NULL) {}
    
    void operator=(const PtrContainer& rhs) {
        if (rhs._ptr) {
            if (_ptr) {
                *_ptr = *rhs._ptr;
            } else {
                _ptr = new T(*rhs._ptr);
            }
        } else {
            delete _ptr;
            _ptr = NULL;
        }
    }

    T* get() const { return _ptr; }

    void reset(T* ptr) {
        delete _ptr;
        _ptr = ptr;
    }

    operator void*() const { return _ptr; }
    
private:
    T* _ptr;
};

}  // namespace butil

#endif  // BUTIL_PTR_CONTAINER_H
