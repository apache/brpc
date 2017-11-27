// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_HASH_H_
#define BUTIL_HASH_H_

#include <limits>
#include <string>

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/logging.h"

namespace butil {

// WARNING: This hash function should not be used for any cryptographic purpose.
BUTIL_EXPORT uint32_t SuperFastHash(const char* data, int len);

// Computes a hash of a memory buffer |data| of a given |length|.
// WARNING: This hash function should not be used for any cryptographic purpose.
inline uint32_t Hash(const char* data, size_t length) {
  if (length > static_cast<size_t>(std::numeric_limits<int>::max())) {
    NOTREACHED();
    return 0;
  }
  return SuperFastHash(data, static_cast<int>(length));
}

// Computes a hash of a string |str|.
// WARNING: This hash function should not be used for any cryptographic purpose.
inline uint32_t Hash(const std::string& str) {
  return Hash(str.data(), str.size());
}

}  // namespace butil

#endif  // BUTIL_HASH_H_
