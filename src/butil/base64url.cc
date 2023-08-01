// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/base64.h"
#include "butil/base64url.h"

#include "third_party/modp_b64/modp_b64_data.h"

namespace butil {

// Base64url maps {+, /} to {-, _} in order for the encoded content to be safe
// to use in a URL. These characters will be translated by this implementation.
#define BASE64_CHARS "+/"
#define BASE64_URL_SAFE_CHARS "-_"
#define URL_SAFE_CHAR62 '-'
#define URL_SAFE_CHAR63 '_'

void Base64UrlEncode(const StringPiece& input,
                     Base64UrlEncodePolicy policy,
                     std::string* output) {
    Base64Encode(input, output);

    std::replace(output->begin(), output->end(), CHAR62, URL_SAFE_CHAR62);
    std::replace(output->begin(), output->end(), CHAR63, URL_SAFE_CHAR63);

    switch (policy) {
        case Base64UrlEncodePolicy::INCLUDE_PADDING:
            // The padding included in |*output| will not be amended.
            break;
        case Base64UrlEncodePolicy::OMIT_PADDING:
            // The padding included in |*output| will be removed.
            const size_t last_non_padding_pos =
                    output->find_last_not_of(CHARPAD);
            if (last_non_padding_pos != std::string::npos) {
                output->resize(last_non_padding_pos + 1);
            }
            break;
    }
}

bool Base64UrlDecode(const StringPiece& input,
                     Base64UrlDecodePolicy policy,
                     std::string* output) {
    // Characters outside of the base64url alphabet are disallowed, which includes
    // the {+, /} characters found in the conventional base64 alphabet.
    if (input.find_first_of(BASE64_CHARS) != std::string::npos)
        return false;

    const size_t required_padding_characters = input.size() % 4;
    const bool needs_replacement =
            input.find_first_of(BASE64_URL_SAFE_CHARS) != std::string::npos;

    switch (policy) {
        case Base64UrlDecodePolicy::REQUIRE_PADDING:
            // Fail if the required padding is not included in |input|.
            if (required_padding_characters > 0)
                return false;
            break;
        case Base64UrlDecodePolicy::IGNORE_PADDING:
            // Missing padding will be silently appended.
            break;
        case Base64UrlDecodePolicy::DISALLOW_PADDING:
            // Fail if padding characters are included in |input|.
            if (input.find_first_of(CHARPAD) != std::string::npos)
                return false;
            break;
    }

    // If the string either needs replacement of URL-safe characters to normal
    // base64 ones, or additional padding, a copy of |input| needs to be made in
    // order to make these adjustments without side effects.
    if (required_padding_characters > 0 || needs_replacement) {
        std::string base64_input;

        size_t base64_input_size = input.size();
        if (required_padding_characters > 0)
            base64_input_size += 4 - required_padding_characters;

        base64_input.reserve(base64_input_size);
        input.AppendToString(&base64_input);

        // Substitute the base64url URL-safe characters to their base64 equivalents.
        std::replace(base64_input.begin(), base64_input.end(), URL_SAFE_CHAR62, CHAR62);
        std::replace(base64_input.begin(), base64_input.end(), URL_SAFE_CHAR63, CHAR63);

        // Append the necessary padding characters.
        base64_input.resize(base64_input_size, '=');

        return Base64Decode(base64_input, output);
    }

    return Base64Decode(input, output);
}

}  // namespace butil
