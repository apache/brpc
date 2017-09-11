// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/guid.h"

namespace butil {

bool IsValidGUID(const std::string& guid) {
  const size_t kGUIDLength = 36U;
  if (guid.length() != kGUIDLength)
    return false;

  const std::string hexchars = "0123456789ABCDEF";
  for (uint32_t i = 0; i < guid.length(); ++i) {
    char current = guid[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (current != '-')
        return false;
    } else {
      if (hexchars.find(current) == std::string::npos)
        return false;
    }
  }

  return true;
}

}  // namespace butil
