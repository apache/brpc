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


#ifndef BUTIL_RELOADABLE_FLAGS_H
#define BUTIL_RELOADABLE_FLAGS_H

#include <stdint.h>
#include <unistd.h>                    // write, _exit
#include <gflags/gflags.h>
#include "butil/macros.h"
#include "butil/type_traits.h"

// Register an always-true valiator to a gflag so that the gflag is treated as
// reloadable by brpc. If a validator exists, abort the program.
// You should call this macro within global scope. for example:
//
//   DEFINE_int32(foo, 0, "blah blah");
//   BRPC_VALIDATE_GFLAG(foo, brpc::PassValidate);
//
// This macro does not work for string-flags because they're thread-unsafe to
// modify directly. To emphasize this, you have to write the validator by
// yourself and use GFLAGS_NAMESPACE::GetCommandLineOption() to acess the flag.
#define BUTIL_VALIDATE_GFLAG(flag, validate_fn)                   \
    namespace butil_flags {}                                      \
    const int ALLOW_UNUSED register_FLAGS_ ## flag ## _dummy =    \
        ::butil::RegisterFlagValidatorOrDieImpl<                  \
            decltype(FLAGS_##flag)>(&FLAGS_##flag, (validate_fn))


namespace butil {

template <typename T>
bool PassValidate(const char*, T) {
    return true;
}

template <typename T>
bool PositiveInteger(const char*, T v) {
    return v > 0;
}

template <typename T>
bool NonNegativeInteger(const char*, T v) {
    return v >= 0;
}

template <typename T>
bool RegisterFlagValidatorOrDieImpl(
    const T* flag, bool (*validate_fn)(const char*, T val)) {
    static_assert(!butil::is_same<std::string, T>::value,
                  "Not support string flags");
    if (::GFLAGS_NAMESPACE::RegisterFlagValidator(flag, validate_fn)) {
        return true;
    }
    // Error printed by gflags does not have newline. Add one to it.
    char newline = '\n';
    butil::ignore_result(write(2, &newline, 1));
    _exit(1);
}

} // namespace butil


#endif  // BUTIL_RELOADABLE_FLAGS_H
