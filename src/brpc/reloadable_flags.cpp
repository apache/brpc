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


#include <unistd.h>                    // write, _exit
#include <gflags/gflags.h>
#include "butil/macros.h"
#include "brpc/reloadable_flags.h"

namespace brpc {

bool PassValidate(const char*, bool) {
    return true;
}
bool PassValidate(const char*, int32_t) {
    return true;
}
bool PassValidate(const char*, int64_t) {
    return true;
}
bool PassValidate(const char*, uint64_t) {
    return true;
}
bool PassValidate(const char*, double) {
    return true;
}

bool PositiveInteger(const char*, int32_t val) {
    return val > 0;
}
bool PositiveInteger(const char*, int64_t val) {
    return val > 0;
}

bool NonNegativeInteger(const char*, int32_t val) {
    return val >= 0;
}
bool NonNegativeInteger(const char*, int64_t val) {
    return val >= 0;
}

template <typename T>
static bool RegisterFlagValidatorOrDieImpl(
    const T* flag, bool (*validate_fn)(const char*, T val)) {
    if (GFLAGS_NS::RegisterFlagValidator(flag, validate_fn)) {
        return true;
    }
    // Error printed by gflags does not have newline. Add one to it.
    char newline = '\n';
    butil::ignore_result(write(2, &newline, 1));
    _exit(1);
}

bool RegisterFlagValidatorOrDie(const bool* flag,
                                bool (*validate_fn)(const char*, bool)) {
    return RegisterFlagValidatorOrDieImpl(flag, validate_fn);
}
bool RegisterFlagValidatorOrDie(const int32_t* flag,
                                bool (*validate_fn)(const char*, int32_t)) {
    return RegisterFlagValidatorOrDieImpl(flag, validate_fn);
}
bool RegisterFlagValidatorOrDie(const int64_t* flag,
                                bool (*validate_fn)(const char*, int64_t)) {
    return RegisterFlagValidatorOrDieImpl(flag, validate_fn);
}
bool RegisterFlagValidatorOrDie(const uint64_t* flag,
                                bool (*validate_fn)(const char*, uint64_t)) {
    return RegisterFlagValidatorOrDieImpl(flag, validate_fn);
}
bool RegisterFlagValidatorOrDie(const double* flag,
                                bool (*validate_fn)(const char*, double)) {
    return RegisterFlagValidatorOrDieImpl(flag, validate_fn);
}

} // namespace brpc
