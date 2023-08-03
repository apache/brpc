// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_BASE64URL_H_
#define BASE_BASE64URL_H_

#include <string>

#include "butil/base_export.h"
#include "butil/strings/string_piece.h"

namespace butil {

enum class Base64UrlEncodePolicy {
    // Include the trailing padding in the output, when necessary.
    INCLUDE_PADDING,

    // Remove the trailing padding from the output.
    OMIT_PADDING
};

// Encodes the |input| string in base64url, defined in RFC 4648:
// https://tools.ietf.org/html/rfc4648#section-5
//
// The |policy| defines whether padding should be included or omitted from the
// encoded |*output|. |input| and |*output| may reference the same storage.
BUTIL_EXPORT void Base64UrlEncode(const StringPiece& input,
                                 Base64UrlEncodePolicy policy,
                                 std::string* output);

enum class Base64UrlDecodePolicy {
    // Require inputs to contain trailing padding if non-aligned.
    REQUIRE_PADDING,

    // Accept inputs regardless of whether they have the correct padding.
    IGNORE_PADDING,

    // Reject inputs if they contain any trailing padding.
    DISALLOW_PADDING
};

// Decodes the |input| string in base64url, defined in RFC 4648:
// https://tools.ietf.org/html/rfc4648#section-5
//
// The |policy| defines whether padding will be required, ignored or disallowed
// altogether. |input| and |*output| may reference the same storage.
BUTIL_EXPORT bool Base64UrlDecode(const StringPiece& input,
                                 Base64UrlDecodePolicy policy,
                                 std::string* output) WARN_UNUSED_RESULT;

}  // namespace butil

#endif  // BASE_BASE64URL_H_
