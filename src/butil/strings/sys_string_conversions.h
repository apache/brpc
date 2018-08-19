// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_STRINGS_SYS_STRING_CONVERSIONS_H_
#define BUTIL_STRINGS_SYS_STRING_CONVERSIONS_H_

// Provides system-dependent string type conversions for cases where it's
// necessary to not use ICU. Generally, you should not need this in Chrome,
// but it is used in some shared code. Dependencies should be minimal.

#include <string>

#include "butil/base_export.h"
#include "butil/basictypes.h"
#include "butil/strings/string16.h"
#include "butil/strings/string_piece.h"

#if defined(OS_MACOSX)
#include <CoreFoundation/CoreFoundation.h>
#ifdef __OBJC__
@class NSString;
#else
class NSString;
#endif
#endif  // OS_MACOSX

namespace butil {

// Converts between wide and UTF-8 representations of a string. On error, the
// result is system-dependent.
BUTIL_EXPORT std::string SysWideToUTF8(const std::wstring& wide);
BUTIL_EXPORT std::wstring SysUTF8ToWide(const StringPiece& utf8);

// Converts between wide and the system multi-byte representations of a string.
// DANGER: This will lose information and can change (on Windows, this can
// change between reboots).
BUTIL_EXPORT std::string SysWideToNativeMB(const std::wstring& wide);
BUTIL_EXPORT std::wstring SysNativeMBToWide(const StringPiece& native_mb);

// Windows-specific ------------------------------------------------------------

#if defined(OS_WIN)

// Converts between 8-bit and wide strings, using the given code page. The
// code page identifier is one accepted by the Windows function
// MultiByteToWideChar().
BUTIL_EXPORT std::wstring SysMultiByteToWide(const StringPiece& mb,
                                            uint32_t code_page);
BUTIL_EXPORT std::string SysWideToMultiByte(const std::wstring& wide,
                                           uint32_t code_page);

#endif  // defined(OS_WIN)

// Mac-specific ----------------------------------------------------------------

#if defined(OS_MACOSX)

// Converts between STL strings and CFStringRefs/NSStrings.

// Creates a string, and returns it with a refcount of 1. You are responsible
// for releasing it. Returns NULL on failure.
BUTIL_EXPORT CFStringRef SysUTF8ToCFStringRef(const std::string& utf8);
BUTIL_EXPORT CFStringRef SysUTF16ToCFStringRef(const string16& utf16);

// Same, but returns an autoreleased NSString.
BUTIL_EXPORT NSString* SysUTF8ToNSString(const std::string& utf8);
BUTIL_EXPORT NSString* SysUTF16ToNSString(const string16& utf16);

// Converts a CFStringRef to an STL string. Returns an empty string on failure.
BUTIL_EXPORT std::string SysCFStringRefToUTF8(CFStringRef ref);
BUTIL_EXPORT string16 SysCFStringRefToUTF16(CFStringRef ref);

// Same, but accepts NSString input. Converts nil NSString* to the appropriate
// string type of length 0.
BUTIL_EXPORT std::string SysNSStringToUTF8(NSString* ref);
BUTIL_EXPORT string16 SysNSStringToUTF16(NSString* ref);

#endif  // defined(OS_MACOSX)

}  // namespace butil

#endif  // BUTIL_STRINGS_SYS_STRING_CONVERSIONS_H_
