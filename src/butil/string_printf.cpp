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

#include <stdio.h>                               // vsnprintf
#include <string.h>                              // strlen
#include "butil/string_printf.h"

namespace butil {

// Copyright 2012 Facebook, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//   http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

namespace {
inline int string_printf_impl(std::string& output, const char* format,
                              va_list args) {
    // Tru to the space at the end of output for our output buffer.
    // Find out write point then inflate its size temporarily to its
    // capacity; we will later shrink it to the size needed to represent
    // the formatted string.  If this buffer isn't large enough, we do a
    // resize and try again.

    const int write_point = output.size();
    int remaining = output.capacity() - write_point;
    output.resize(output.capacity());

    va_list copied_args;
    va_copy(copied_args, args);
    int bytes_used = vsnprintf(&output[write_point], remaining, format,
                               copied_args);
    va_end(copied_args);
    if (bytes_used < 0) {
        return -1;
    } else if (bytes_used < remaining) {
        // There was enough room, just shrink and return.
        output.resize(write_point + bytes_used);
    } else {
        output.resize(write_point + bytes_used + 1);
        remaining = bytes_used + 1;
        bytes_used = vsnprintf(&output[write_point], remaining, format, args);
        if (bytes_used + 1 != remaining) {
            return -1;
        }
        output.resize(write_point + bytes_used);
    }
    return 0;
}

inline int string_printf_impl(std::string& output,  size_t hint,
                              const char* format, va_list args) {
    if (hint > output.capacity()) {
        output.reserve(hint);
    }
    return string_printf_impl(output,  format, args);
}
}  // end anonymous namespace

std::string string_printf(const char* format, ...) {
    // snprintf will tell us how large the output buffer should be, but
    // we then have to call it a second time, which is costly. By
    // guestimating the final size, we avoid the double snprintf in many
    // cases, resulting in a performance win. We use this constructor
    // of std::string to avoid a double allocation, though it does pad
    // the resulting string with nul bytes. Our guestimation is twice
    // the format string size, or 32 bytes, whichever is larger.  This
    // is a heuristic that doesn't affect correctness but attempts to be
    // reasonably fast for the most common cases.
    std::string ret;
    va_list ap;
    va_start(ap, format);
    if (string_printf_impl(ret, std::max(32UL, strlen(format) * 2),
                           format, ap) != 0) {
        ret.clear();
    }
    va_end(ap);
    return ret;
}

std::string string_printf(size_t hint_size, const char* format, ...) {
    // snprintf will tell us how large the output buffer should be, but
    // we then have to call it a second time, which is costly. By
    // passing the hint size of formatted string, we avoid the double
    // snprintf in many cases, resulting in a performance win. We use
    // this constructor  of std::string to avoid a double allocation,
    // though it does pad the resulting string with nul bytes.
    std::string ret;
    va_list ap;
    va_start(ap, format);
    if (string_printf_impl(ret, std::max(hint_size, strlen(format) * 2),
                           format, ap) != 0) {
        ret.clear();
    }
    va_end(ap);
    return ret;
}

// Basic declarations; allow for parameters of strings and string
// pieces to be specified.
int string_appendf(std::string* output, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int rc = string_vappendf(output, format, ap);
    va_end(ap);
    return rc;
}

int string_vappendf(std::string* output, const char* format, va_list args) {
    const size_t old_size = output->size();
    const int rc = string_printf_impl(*output, format, args);
    if (rc != 0) {
        output->resize(old_size);
    }
    return rc;
}

int string_printf(std::string* output, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    const int rc = string_vprintf(output, format, ap);
    va_end(ap);
    return rc;
};

int string_vprintf(std::string* output, const char* format, va_list args) {
    output->clear();
    const int rc = string_printf_impl(*output, format, args);
    if (rc != 0) {
        output->clear();
    }
    return rc;
};

}  // namespace butil
