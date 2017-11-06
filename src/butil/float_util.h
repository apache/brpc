// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_FLOAT_UTIL_H_
#define BUTIL_FLOAT_UTIL_H_

#include "butil/build_config.h"

#include <float.h>

#include <cmath>

namespace butil {

template <typename Float>
inline bool IsFinite(const Float& number) {
#if defined(OS_POSIX)
  return std::isfinite(number) != 0;
#elif defined(OS_WIN)
  return _finite(number) != 0;
#endif
}

template <typename Float>
inline bool IsNaN(const Float& number) {
#if defined(OS_POSIX)
  return std::isnan(number) != 0;
#elif defined(OS_WIN)
  return _isnan(number) != 0;
#endif
}

}  // namespace butil

#endif  // BUTIL_FLOAT_UTIL_H_
