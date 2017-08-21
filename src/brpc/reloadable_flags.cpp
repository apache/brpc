// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#include "base/macros.h"
#include "brpc/reloadable_flags.h"

namespace google {
extern bool RegisterFlagValidator(const bool* flag,
                                  bool (*validate_fn)(const char*, bool));
extern bool RegisterFlagValidator(const int32_t* flag,
                                  bool (*validate_fn)(const char*, int32_t));
extern bool RegisterFlagValidator(const int64_t* flag,
                                  bool (*validate_fn)(const char*, int64_t));
extern bool RegisterFlagValidator(const uint64_t* flag,
                                  bool (*validate_fn)(const char*, uint64_t));
extern bool RegisterFlagValidator(const double* flag,
                                  bool (*validate_fn)(const char*, double));
} // namespace google


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
    if (::google::RegisterFlagValidator(flag, validate_fn)) {
        return true;
    }
    // Error printed by gflags does not have newline. Add one to it.
    char newline = '\n';
    base::ignore_result(write(2, &newline, 1));
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

