// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_DUMP_WITHOUT_CRASHING_H_
#define BASE_DEBUG_DUMP_WITHOUT_CRASHING_H_

#include "butil/base_export.h"
#include "butil/compiler_specific.h"
#include "butil/build_config.h"

namespace butil {

namespace debug {

// Handler to silently dump the current process without crashing.
BASE_EXPORT void DumpWithoutCrashing();

// Sets a function that'll be invoked to dump the current process when
// DumpWithoutCrashing() is called.
BASE_EXPORT void SetDumpWithoutCrashingFunction(void (CDECL *function)());

}  // namespace debug

}  // namespace butil

#endif  // BASE_DEBUG_DUMP_WITHOUT_CRASHING_H_
