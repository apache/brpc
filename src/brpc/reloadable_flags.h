// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Sun Aug 31 16:27:49 CST 2014

#ifndef BRPC_RELOADABLE_FLAGS_H
#define BRPC_RELOADABLE_FLAGS_H

// To baidu-rpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include <stdint.h>

// Register an always-true valiator to a gflag so that the gflag is treated as
// reloadable by baidu-rpc. If a validator exists, abort the program.
// You should call this macro within global scope. for example:
//
//   DEFINE_int32(foo, 0, "blah blah");
//   BRPC_VALIDATE_GFLAG(foo, brpc::PassValidate);
//
// This macro does not work for string-flags because they're thread-unsafe to
// modify directly. To emphasize this, you have to write the validator by
// yourself and use google::GetCommandLineOption() to acess the flag.
#define BRPC_VALIDATE_GFLAG(flag, validate_fn)                     \
    const int register_FLAGS_ ## flag ## _dummy                         \
                 __attribute__((__unused__)) =                          \
        ::brpc::RegisterFlagValidatorOrDie(                       \
            &FLAGS_##flag, (validate_fn))


namespace brpc {

extern bool PassValidate(const char*, bool);
extern bool PassValidate(const char*, int32_t);
extern bool PassValidate(const char*, int64_t);
extern bool PassValidate(const char*, uint64_t);
extern bool PassValidate(const char*, double);

extern bool PositiveInteger(const char*, int32_t);
extern bool PositiveInteger(const char*, int64_t);

extern bool NonNegativeInteger(const char*, int32_t);
extern bool NonNegativeInteger(const char*, int64_t);

extern bool RegisterFlagValidatorOrDie(const bool* flag,
                                  bool (*validate_fn)(const char*, bool));
extern bool RegisterFlagValidatorOrDie(const int32_t* flag,
                                  bool (*validate_fn)(const char*, int32_t));
extern bool RegisterFlagValidatorOrDie(const int64_t* flag,
                                  bool (*validate_fn)(const char*, int64_t));
extern bool RegisterFlagValidatorOrDie(const uint64_t* flag,
                                  bool (*validate_fn)(const char*, uint64_t));
extern bool RegisterFlagValidatorOrDie(const double* flag,
                                  bool (*validate_fn)(const char*, double));
} // namespace brpc


#endif  // BRPC_RELOADABLE_FLAGS_H
