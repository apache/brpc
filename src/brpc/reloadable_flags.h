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


#ifndef BRPC_RELOADABLE_FLAGS_H
#define BRPC_RELOADABLE_FLAGS_H

#include "butil/reloadable_flags.h"

#define BRPC_VALIDATE_GFLAG(flag, validate_fn) BUTIL_VALIDATE_GFLAG(flag, validate_fn)

namespace brpc {

extern bool PassValidate(const char*, bool);
extern bool PassValidate(const char*, int32_t);
extern bool PassValidate(const char*, uint32_t);
extern bool PassValidate(const char*, int64_t);
extern bool PassValidate(const char*, uint64_t);
extern bool PassValidate(const char*, double);

extern bool PositiveInteger(const char*, int32_t);
extern bool PositiveInteger(const char*, uint32_t);
extern bool PositiveInteger(const char*, int64_t);
extern bool PositiveInteger(const char*, uint64_t);

extern bool NonNegativeInteger(const char*, int32_t);
extern bool NonNegativeInteger(const char*, int64_t);

extern bool RegisterFlagValidatorOrDie(
    const bool* flag, bool (*validate_fn)(const char*, bool));
extern bool RegisterFlagValidatorOrDie(
    const int32_t* flag, bool (*validate_fn)(const char*, int32_t));
extern bool RegisterFlagValidatorOrDie(
    const uint32_t* flag, bool (*validate_fn)(const char*, uint32_t));
extern bool RegisterFlagValidatorOrDie(
    const int64_t* flag, bool (*validate_fn)(const char*, int64_t));
extern bool RegisterFlagValidatorOrDie(
    const uint64_t* flag, bool (*validate_fn)(const char*, uint64_t));
extern bool RegisterFlagValidatorOrDie(
    const double* flag, bool (*validate_fn)(const char*, double));
} // namespace brpc


#endif  // BRPC_RELOADABLE_FLAGS_H
