// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_GUID_H_
#define BUTIL_GUID_H_

#include <string>

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/build_config.h"

namespace butil {

// Generate a 128-bit random GUID of the form: "%08X-%04X-%04X-%04X-%012llX".
// If GUID generation fails an empty string is returned.
// The POSIX implementation uses psuedo random number generation to create
// the GUID.  The Windows implementation uses system services.
BUTIL_EXPORT std::string GenerateGUID();

// Returns true if the input string conforms to the GUID format.
BUTIL_EXPORT bool IsValidGUID(const std::string& guid);

#if defined(OS_POSIX)
// For unit testing purposes only.  Do not use outside of tests.
BUTIL_EXPORT std::string RandomDataToGUIDString(const uint64_t bytes[2]);
#endif

}  // namespace butil

#endif  // BUTIL_GUID_H_
