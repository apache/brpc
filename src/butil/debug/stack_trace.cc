// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/debug/stack_trace.h"

#include "butil/basictypes.h"

#include <string.h>

#include <algorithm>
#include <sstream>

namespace butil {
namespace debug {

StackTrace::StackTrace(const void* const* trace, size_t count) {
  count = std::min(count, arraysize(trace_));
  if (count)
    memcpy(trace_, trace, count * sizeof(trace_[0]));
  count_ = count;
}

const void *const *StackTrace::Addresses(size_t* count) const {
  *count = count_;
  if (count_)
    return trace_;
  return NULL;
}

size_t StackTrace::CopyAddressTo(void** buffer, size_t max_nframes) const {
  size_t nframes = std::min(count_, max_nframes);
  memcpy(buffer, trace_, nframes * sizeof(void*));
  return nframes;
}

std::string StackTrace::ToString() const {
  std::string str;
  str.reserve(1024);
#if !defined(__UCLIBC__)
  OutputToString(str);
#endif
  return str;
}

}  // namespace debug
}  // namespace butil
