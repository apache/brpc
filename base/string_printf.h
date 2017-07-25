// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved
//
// Format std::string.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BRPC_BASE_STRING_PRINTF_H
#define BRPC_BASE_STRING_PRINTF_H

#include <string>                                // std::string
#include <stdarg.h>                              // va_list

namespace base {

// Convert |format| and associated arguments to std::string
std::string string_printf(const char* format, ...)
    __attribute__ ((format (printf, 1, 2)));

// Write |format| and associated arguments into |output|
// Returns 0 on success, -1 otherwise.
int string_printf(std::string* output, const char* fmt, ...)
    __attribute__ ((format (printf, 2, 3)));

// Write |format| and associated arguments in form of va_list into |output|.
// Returns 0 on success, -1 otherwise.
int string_vprintf(std::string* output, const char* format, va_list args);

// Append |format| and associated arguments to |output|
// Returns 0 on success, -1 otherwise.
int string_appendf(std::string* output, const char* format, ...)
    __attribute__ ((format (printf, 2, 3)));

// Append |format| and associated arguments in form of va_list to |output|.
// Returns 0 on success, -1 otherwise.
int string_vappendf(std::string* output, const char* format, va_list args);


}  // namespace base

#endif  // BRPC_BASE_STRING_PRINTF_H
