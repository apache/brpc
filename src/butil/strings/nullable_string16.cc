// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/strings/nullable_string16.h"

#include <ostream>

#include "butil/strings/utf_string_conversions.h"

namespace butil {

std::ostream& operator<<(std::ostream& out, const NullableString16& value) {
  return value.is_null() ? out << "(null)" : out << UTF16ToUTF8(value.string());
}

}  // namespace butil
