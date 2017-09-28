// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/hash.h"

// Definition in third_party/superfasthash/superfasthash.c. (Third-party
// code did not come with its own header file, so declaring the function here.)
// Note: This algorithm is also in Blink under Source/wtf/StringHasher.h.
extern "C" uint32_t SuperFastHash(const char* data, int len);

namespace butil {

uint32_t SuperFastHash(const char* data, int len) {
  return ::SuperFastHash(data, len);
}

}  // namespace butil
