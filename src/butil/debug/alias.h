// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_DEBUG_ALIAS_H_
#define BUTIL_DEBUG_ALIAS_H_

#include "butil/base_export.h"

namespace butil {
namespace debug {

// Make the optimizer think that var is aliased. This is to prevent it from
// optimizing out variables that that would not otherwise be live at the point
// of a potential crash.
void BUTIL_EXPORT Alias(const void* var);

}  // namespace debug
}  // namespace butil

#endif  // BUTIL_DEBUG_ALIAS_H_
