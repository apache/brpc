// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_STRINGS_STRING_SPLIT_H_
#define BUTIL_STRINGS_STRING_SPLIT_H_

#include <string>
#include <utility>
#include <vector>

#include "butil/base_export.h"
#include "butil/strings/string16.h"
#include "butil/strings/string_piece.h"

namespace butil {

// Splits |str| into a vector of strings delimited by |c|, placing the results
// in |r|. If several instances of |c| are contiguous, or if |str| begins with
// or ends with |c|, then an empty string is inserted.
//
// Every substring is trimmed of any leading or trailing white space.
// NOTE: |c| must be in BMP (Basic Multilingual Plane)
BUTIL_EXPORT void SplitString(const string16& str,
                             char16 c,
                             std::vector<string16>* r);
BUTIL_EXPORT void SplitString(const butil::StringPiece16& str,
                             char16 c,
                             std::vector<butil::StringPiece16>* r);

// |str| should not be in a multi-byte encoding like Shift-JIS or GBK in which
// the trailing byte of a multi-byte character can be in the ASCII range.
// UTF-8, and other single/multi-byte ASCII-compatible encodings are OK.
// Note: |c| must be in the ASCII range.
BUTIL_EXPORT void SplitString(const std::string& str,
                             char c,
                             std::vector<std::string>* r);
BUTIL_EXPORT void SplitString(const butil::StringPiece& str,
                             char c,
                             std::vector<butil::StringPiece>* r);

typedef std::vector<std::pair<std::string, std::string> > StringPairs;
typedef std::vector<std::pair<butil::StringPiece, butil::StringPiece> > StringPiecePairs;

// Splits |line| into key value pairs according to the given delimiters and
// removes whitespace leading each key and trailing each value. Returns true
// only if each pair has a non-empty key and value. |key_value_pairs| will
// include ("","") pairs for entries without |key_value_delimiter|.
BUTIL_EXPORT bool SplitStringIntoKeyValuePairs(const std::string& line,
                                              char key_value_delimiter,
                                              char key_value_pair_delimiter,
                                              StringPairs* key_value_pairs);
BUTIL_EXPORT bool SplitStringIntoKeyValuePairs(const butil::StringPiece& line,
                                              char key_value_delimiter,
                                              char key_value_pair_delimiter,
                                              StringPiecePairs* key_value_pairs);

// The same as SplitString, but use a substring delimiter instead of a char.
BUTIL_EXPORT void SplitStringUsingSubstr(const string16& str,
                                        const string16& s,
                                        std::vector<string16>* r);
BUTIL_EXPORT void SplitStringUsingSubstr(const butil::StringPiece16& str,
                                        const butil::StringPiece16& s,
                                        std::vector<butil::StringPiece16>* r);
BUTIL_EXPORT void SplitStringUsingSubstr(const std::string& str,
                                        const std::string& s,
                                        std::vector<std::string>* r);
BUTIL_EXPORT void SplitStringUsingSubstr(const butil::StringPiece& str,
                                        const butil::StringPiece& s,
                                        std::vector<butil::StringPiece>* r);

// The same as SplitString, but don't trim white space.
// NOTE: |c| must be in BMP (Basic Multilingual Plane)
BUTIL_EXPORT void SplitStringDontTrim(const string16& str,
                                     char16 c,
                                     std::vector<string16>* r);
BUTIL_EXPORT void SplitStringDontTrim(const butil::StringPiece16& str,
                                     char16 c,
                                     std::vector<butil::StringPiece16>* r);
// |str| should not be in a multi-byte encoding like Shift-JIS or GBK in which
// the trailing byte of a multi-byte character can be in the ASCII range.
// UTF-8, and other single/multi-byte ASCII-compatible encodings are OK.
// Note: |c| must be in the ASCII range.
BUTIL_EXPORT void SplitStringDontTrim(const std::string& str,
                                     char c,
                                     std::vector<std::string>* r);
BUTIL_EXPORT void SplitStringDontTrim(const butil::StringPiece& str,
                                     char c,
                                     std::vector<butil::StringPiece>* r);

// WARNING: this uses whitespace as defined by the HTML5 spec. If you need
// a function similar to this but want to trim all types of whitespace, then
// factor this out into a function that takes a string containing the characters
// that are treated as whitespace.
//
// Splits the string along whitespace (where whitespace is the five space
// characters defined by HTML 5). Each contiguous block of non-whitespace
// characters is added to result.
BUTIL_EXPORT void SplitStringAlongWhitespace(const string16& str,
                                            std::vector<string16>* result);
BUTIL_EXPORT void SplitStringAlongWhitespace(const butil::StringPiece16& str,
                                            std::vector<butil::StringPiece16>* result);
BUTIL_EXPORT void SplitStringAlongWhitespace(const std::string& str,
                                            std::vector<std::string>* result);
BUTIL_EXPORT void SplitStringAlongWhitespace(const butil::StringPiece& str,
                                            std::vector<butil::StringPiece>* result);

}  // namespace butil

#endif  // BUTIL_STRINGS_STRING_SPLIT_H_
