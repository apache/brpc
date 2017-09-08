// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/strings/string_util.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#include <algorithm>
#include <vector>

#include "butil/basictypes.h"
#include "butil/logging.h"
#include "butil/memory/singleton.h"
#include "butil/strings/utf_string_conversion_utils.h"
#include "butil/strings/utf_string_conversions.h"
#include "butil/third_party/icu/icu_utf.h"
#include "butil/build_config.h"

// Remove when this entire file is in namespace butil.
using butil::char16;
using butil::string16;

namespace {

// Force the singleton used by EmptyString[16] to be a unique type. This
// prevents other code that might accidentally use Singleton<string> from
// getting our internal one.
struct EmptyStrings {
  EmptyStrings() {}
  const std::string s;
  const string16 s16;

  static EmptyStrings* GetInstance() {
    return Singleton<EmptyStrings>::get();
  }
};

// Used by ReplaceStringPlaceholders to track the position in the string of
// replaced parameters.
struct ReplacementOffset {
  ReplacementOffset(uintptr_t parameter, size_t offset)
      : parameter(parameter),
        offset(offset) {}

  // Index of the parameter.
  uintptr_t parameter;

  // Starting position in the string.
  size_t offset;
};

static bool CompareParameter(const ReplacementOffset& elem1,
                             const ReplacementOffset& elem2) {
  return elem1.parameter < elem2.parameter;
}

}  // namespace

namespace butil {

bool IsWprintfFormatPortable(const wchar_t* format) {
  for (const wchar_t* position = format; *position != '\0'; ++position) {
    if (*position == '%') {
      bool in_specification = true;
      bool modifier_l = false;
      while (in_specification) {
        // Eat up characters until reaching a known specifier.
        if (*++position == '\0') {
          // The format string ended in the middle of a specification.  Call
          // it portable because no unportable specifications were found.  The
          // string is equally broken on all platforms.
          return true;
        }

        if (*position == 'l') {
          // 'l' is the only thing that can save the 's' and 'c' specifiers.
          modifier_l = true;
        } else if (((*position == 's' || *position == 'c') && !modifier_l) ||
                   *position == 'S' || *position == 'C' || *position == 'F' ||
                   *position == 'D' || *position == 'O' || *position == 'U') {
          // Not portable.
          return false;
        }

        if (wcschr(L"diouxXeEfgGaAcspn%", *position)) {
          // Portable, keep scanning the rest of the format string.
          in_specification = false;
        }
      }
    }
  }

  return true;
}

const std::string& EmptyString() {
  return EmptyStrings::GetInstance()->s;
}

const string16& EmptyString16() {
  return EmptyStrings::GetInstance()->s16;
}

template<typename STR>
bool ReplaceCharsT(const STR& input,
                   const STR& replace_chars,
                   const STR& replace_with,
                   STR* output) {
  bool removed = false;
  size_t replace_length = replace_with.length();

  *output = input;

  size_t found = output->find_first_of(replace_chars);
  while (found != STR::npos) {
    removed = true;
    output->replace(found, 1, replace_with);
    found = output->find_first_of(replace_chars, found + replace_length);
  }

  return removed;
}

bool ReplaceChars(const string16& input,
                  const butil::StringPiece16& replace_chars,
                  const string16& replace_with,
                  string16* output) {
  return ReplaceCharsT(input, replace_chars.as_string(), replace_with, output);
}

bool ReplaceChars(const std::string& input,
                  const butil::StringPiece& replace_chars,
                  const std::string& replace_with,
                  std::string* output) {
  return ReplaceCharsT(input, replace_chars.as_string(), replace_with, output);
}

bool RemoveChars(const string16& input,
                 const butil::StringPiece16& remove_chars,
                 string16* output) {
  return ReplaceChars(input, remove_chars.as_string(), string16(), output);
}

bool RemoveChars(const std::string& input,
                 const butil::StringPiece& remove_chars,
                 std::string* output) {
  return ReplaceChars(input, remove_chars.as_string(), std::string(), output);
}

template<typename STR>
TrimPositions TrimStringT(const STR& input,
                          const STR& trim_chars,
                          TrimPositions positions,
                          STR* output) {
  // Find the edges of leading/trailing whitespace as desired.
  const size_t last_char = input.length() - 1;
  const size_t first_good_char = (positions & TRIM_LEADING) ?
      input.find_first_not_of(trim_chars) : 0;
  const size_t last_good_char = (positions & TRIM_TRAILING) ?
      input.find_last_not_of(trim_chars) : last_char;

  // When the string was all whitespace, report that we stripped off whitespace
  // from whichever position the caller was interested in.  For empty input, we
  // stripped no whitespace, but we still need to clear |output|.
  if (input.empty() ||
      (first_good_char == STR::npos) || (last_good_char == STR::npos)) {
    bool input_was_empty = input.empty();  // in case output == &input
    output->clear();
    return input_was_empty ? TRIM_NONE : positions;
  }

  // Trim the whitespace.
  *output =
      input.substr(first_good_char, last_good_char - first_good_char + 1);

  // Return where we trimmed from.
  return static_cast<TrimPositions>(
      ((first_good_char == 0) ? TRIM_NONE : TRIM_LEADING) |
      ((last_good_char == last_char) ? TRIM_NONE : TRIM_TRAILING));
}

bool TrimString(const string16& input,
                const butil::StringPiece16& trim_chars,
                string16* output) {
  return TrimStringT(input, trim_chars.as_string(), TRIM_ALL, output) !=
      TRIM_NONE;
}

bool TrimString(const std::string& input,
                const butil::StringPiece& trim_chars,
                std::string* output) {
  return TrimStringT(input, trim_chars.as_string(), TRIM_ALL, output) !=
      TRIM_NONE;
}

void TruncateUTF8ToByteSize(const std::string& input,
                            const size_t byte_size,
                            std::string* output) {
  DCHECK(output);
  if (byte_size > input.length()) {
    *output = input;
    return;
  }
  DCHECK_LE(byte_size, static_cast<uint32_t>(kint32max));
  // Note: This cast is necessary because CBU8_NEXT uses int32s.
  int32_t truncation_length = static_cast<int32_t>(byte_size);
  int32_t char_index = truncation_length - 1;
  const char* data = input.data();

  // Using CBU8, we will move backwards from the truncation point
  // to the beginning of the string looking for a valid UTF8
  // character.  Once a full UTF8 character is found, we will
  // truncate the string to the end of that character.
  while (char_index >= 0) {
    int32_t prev = char_index;
    uint32_t code_point = 0;
    CBU8_NEXT(data, char_index, truncation_length, code_point);
    if (!IsValidCharacter(code_point) ||
        !IsValidCodepoint(code_point)) {
      char_index = prev - 1;
    } else {
      break;
    }
  }

  if (char_index >= 0 )
    *output = input.substr(0, char_index);
  else
    output->clear();
}

TrimPositions TrimWhitespace(const string16& input,
                             TrimPositions positions,
                             string16* output) {
  return TrimStringT(input, butil::string16(kWhitespaceUTF16), positions,
                     output);
}

TrimPositions TrimWhitespaceASCII(const std::string& input,
                                  TrimPositions positions,
                                  std::string* output) {
  return TrimStringT(input, std::string(kWhitespaceASCII), positions, output);
}

// This function is only for backward-compatibility.
// To be removed when all callers are updated.
TrimPositions TrimWhitespace(const std::string& input,
                             TrimPositions positions,
                             std::string* output) {
  return TrimWhitespaceASCII(input, positions, output);
}

template<typename STR>
STR CollapseWhitespaceT(const STR& text,
                        bool trim_sequences_with_line_breaks) {
  STR result;
  result.resize(text.size());

  // Set flags to pretend we're already in a trimmed whitespace sequence, so we
  // will trim any leading whitespace.
  bool in_whitespace = true;
  bool already_trimmed = true;

  int chars_written = 0;
  for (typename STR::const_iterator i(text.begin()); i != text.end(); ++i) {
    if (IsWhitespace(*i)) {
      if (!in_whitespace) {
        // Reduce all whitespace sequences to a single space.
        in_whitespace = true;
        result[chars_written++] = L' ';
      }
      if (trim_sequences_with_line_breaks && !already_trimmed &&
          ((*i == '\n') || (*i == '\r'))) {
        // Whitespace sequences containing CR or LF are eliminated entirely.
        already_trimmed = true;
        --chars_written;
      }
    } else {
      // Non-whitespace chracters are copied straight across.
      in_whitespace = false;
      already_trimmed = false;
      result[chars_written++] = *i;
    }
  }

  if (in_whitespace && !already_trimmed) {
    // Any trailing whitespace is eliminated.
    --chars_written;
  }

  result.resize(chars_written);
  return result;
}

string16 CollapseWhitespace(const string16& text,
                            bool trim_sequences_with_line_breaks) {
  return CollapseWhitespaceT(text, trim_sequences_with_line_breaks);
}

std::string CollapseWhitespaceASCII(const std::string& text,
                                    bool trim_sequences_with_line_breaks) {
  return CollapseWhitespaceT(text, trim_sequences_with_line_breaks);
}

bool ContainsOnlyChars(const StringPiece& input,
                       const StringPiece& characters) {
  return input.find_first_not_of(characters) == StringPiece::npos;
}

bool ContainsOnlyChars(const StringPiece16& input,
                       const StringPiece16& characters) {
  return input.find_first_not_of(characters) == StringPiece16::npos;
}

template<class STR>
static bool DoIsStringASCII(const STR& str) {
  for (size_t i = 0; i < str.length(); i++) {
    typename ToUnsigned<typename STR::value_type>::Unsigned c = str[i];
    if (c > 0x7F)
      return false;
  }
  return true;
}

bool IsStringASCII(const StringPiece& str) {
  return DoIsStringASCII(str);
}

bool IsStringASCII(const string16& str) {
  return DoIsStringASCII(str);
}

bool IsStringUTF8(const std::string& str) {
  const char *src = str.data();
  int32_t src_len = static_cast<int32_t>(str.length());
  int32_t char_index = 0;

  while (char_index < src_len) {
    int32_t code_point;
    CBU8_NEXT(src, char_index, src_len, code_point);
    if (!IsValidCharacter(code_point))
      return false;
  }
  return true;
}

}  // namespace butil

template<typename Iter>
static inline bool DoLowerCaseEqualsASCII(Iter a_begin,
                                          Iter a_end,
                                          const char* b) {
  for (Iter it = a_begin; it != a_end; ++it, ++b) {
    if (!*b || butil::ToLowerASCII(*it) != *b)
      return false;
  }
  return *b == 0;
}

// Front-ends for LowerCaseEqualsASCII.
bool LowerCaseEqualsASCII(const std::string& a, const char* b) {
  return DoLowerCaseEqualsASCII(a.begin(), a.end(), b);
}

bool LowerCaseEqualsASCII(const string16& a, const char* b) {
  return DoLowerCaseEqualsASCII(a.begin(), a.end(), b);
}

bool LowerCaseEqualsASCII(std::string::const_iterator a_begin,
                          std::string::const_iterator a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

bool LowerCaseEqualsASCII(string16::const_iterator a_begin,
                          string16::const_iterator a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

// TODO(port): Resolve wchar_t/iterator issues that require OS_ANDROID here.
#if !defined(OS_ANDROID)
bool LowerCaseEqualsASCII(const char* a_begin,
                          const char* a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

bool LowerCaseEqualsASCII(const char16* a_begin,
                          const char16* a_end,
                          const char* b) {
  return DoLowerCaseEqualsASCII(a_begin, a_end, b);
}

#endif  // !defined(OS_ANDROID)

bool EqualsASCII(const string16& a, const butil::StringPiece& b) {
  if (a.length() != b.length())
    return false;
  return std::equal(b.begin(), b.end(), a.begin());
}

bool StartsWithASCII(const std::string& str,
                     const std::string& search,
                     bool case_sensitive) {
  if (case_sensitive)
    return str.compare(0, search.length(), search) == 0;
  else
    return butil::strncasecmp(str.c_str(), search.c_str(), search.length()) == 0;
}

template <typename STR>
bool StartsWithT(const STR& str, const STR& search, bool case_sensitive) {
  if (case_sensitive) {
    return str.compare(0, search.length(), search) == 0;
  } else {
    if (search.size() > str.size())
      return false;
    return std::equal(search.begin(), search.end(), str.begin(),
                      butil::CaseInsensitiveCompare<typename STR::value_type>());
  }
}

bool StartsWith(const string16& str, const string16& search,
                bool case_sensitive) {
  return StartsWithT(str, search, case_sensitive);
}

template <typename STR>
bool EndsWithT(const STR& str, const STR& search, bool case_sensitive) {
  size_t str_length = str.length();
  size_t search_length = search.length();
  if (search_length > str_length)
    return false;
  if (case_sensitive)
    return str.compare(str_length - search_length, search_length, search) == 0;
  return std::equal(search.begin(), search.end(),
                    str.begin() + (str_length - search_length),
                    butil::CaseInsensitiveCompare<typename STR::value_type>());
}

bool EndsWith(const std::string& str, const std::string& search,
              bool case_sensitive) {
  return EndsWithT(str, search, case_sensitive);
}

bool EndsWith(const string16& str, const string16& search,
              bool case_sensitive) {
  return EndsWithT(str, search, case_sensitive);
}

static const char* const kByteStringsUnlocalized[] = {
  " B",
  " kB",
  " MB",
  " GB",
  " TB",
  " PB"
};

string16 FormatBytesUnlocalized(int64_t bytes) {
  double unit_amount = static_cast<double>(bytes);
  size_t dimension = 0;
  const int kKilo = 1024;
  while (unit_amount >= kKilo &&
         dimension < arraysize(kByteStringsUnlocalized) - 1) {
    unit_amount /= kKilo;
    dimension++;
  }

  char buf[64];
  if (bytes != 0 && dimension > 0 && unit_amount < 100) {
    butil::snprintf(buf, arraysize(buf), "%.1lf%s", unit_amount,
                   kByteStringsUnlocalized[dimension]);
  } else {
    butil::snprintf(buf, arraysize(buf), "%.0lf%s", unit_amount,
                   kByteStringsUnlocalized[dimension]);
  }

  return butil::ASCIIToUTF16(buf);
}

template<class StringType>
void DoReplaceSubstringsAfterOffset(StringType* str,
                                    size_t start_offset,
                                    const StringType& find_this,
                                    const StringType& replace_with,
                                    bool replace_all) {
  if ((start_offset == StringType::npos) || (start_offset >= str->length()))
    return;

  DCHECK(!find_this.empty());
  for (size_t offs(str->find(find_this, start_offset));
      offs != StringType::npos; offs = str->find(find_this, offs)) {
    str->replace(offs, find_this.length(), replace_with);
    offs += replace_with.length();

    if (!replace_all)
      break;
  }
}

void ReplaceFirstSubstringAfterOffset(string16* str,
                                      size_t start_offset,
                                      const string16& find_this,
                                      const string16& replace_with) {
  DoReplaceSubstringsAfterOffset(str, start_offset, find_this, replace_with,
                                 false);  // replace first instance
}

void ReplaceFirstSubstringAfterOffset(std::string* str,
                                      size_t start_offset,
                                      const std::string& find_this,
                                      const std::string& replace_with) {
  DoReplaceSubstringsAfterOffset(str, start_offset, find_this, replace_with,
                                 false);  // replace first instance
}

void ReplaceSubstringsAfterOffset(string16* str,
                                  size_t start_offset,
                                  const string16& find_this,
                                  const string16& replace_with) {
  DoReplaceSubstringsAfterOffset(str, start_offset, find_this, replace_with,
                                 true);  // replace all instances
}

void ReplaceSubstringsAfterOffset(std::string* str,
                                  size_t start_offset,
                                  const std::string& find_this,
                                  const std::string& replace_with) {
  DoReplaceSubstringsAfterOffset(str, start_offset, find_this, replace_with,
                                 true);  // replace all instances
}


template<typename STR>
static size_t TokenizeT(const STR& str,
                        const STR& delimiters,
                        std::vector<STR>* tokens) {
  tokens->clear();

  size_t start = str.find_first_not_of(delimiters);
  while (start != STR::npos) {
    size_t end = str.find_first_of(delimiters, start + 1);
    if (end == STR::npos) {
      tokens->push_back(str.substr(start));
      break;
    } else {
      tokens->push_back(str.substr(start, end - start));
      start = str.find_first_not_of(delimiters, end + 1);
    }
  }

  return tokens->size();
}

size_t Tokenize(const string16& str,
                const string16& delimiters,
                std::vector<string16>* tokens) {
  return TokenizeT(str, delimiters, tokens);
}

size_t Tokenize(const std::string& str,
                const std::string& delimiters,
                std::vector<std::string>* tokens) {
  return TokenizeT(str, delimiters, tokens);
}

size_t Tokenize(const butil::StringPiece& str,
                const butil::StringPiece& delimiters,
                std::vector<butil::StringPiece>* tokens) {
  return TokenizeT(str, delimiters, tokens);
}

template<typename STR>
static STR JoinStringT(const std::vector<STR>& parts, const STR& sep) {
  if (parts.empty())
    return STR();

  STR result(parts[0]);
  typename std::vector<STR>::const_iterator iter = parts.begin();
  ++iter;

  for (; iter != parts.end(); ++iter) {
    result += sep;
    result += *iter;
  }

  return result;
}

std::string JoinString(const std::vector<std::string>& parts, char sep) {
  return JoinStringT(parts, std::string(1, sep));
}

string16 JoinString(const std::vector<string16>& parts, char16 sep) {
  return JoinStringT(parts, string16(1, sep));
}

std::string JoinString(const std::vector<std::string>& parts,
                       const std::string& separator) {
  return JoinStringT(parts, separator);
}

string16 JoinString(const std::vector<string16>& parts,
                    const string16& separator) {
  return JoinStringT(parts, separator);
}

template<class FormatStringType, class OutStringType>
OutStringType DoReplaceStringPlaceholders(const FormatStringType& format_string,
    const std::vector<OutStringType>& subst, std::vector<size_t>* offsets) {
  size_t substitutions = subst.size();

  size_t sub_length = 0;
  for (typename std::vector<OutStringType>::const_iterator iter = subst.begin();
       iter != subst.end(); ++iter) {
    sub_length += iter->length();
  }

  OutStringType formatted;
  formatted.reserve(format_string.length() + sub_length);

  std::vector<ReplacementOffset> r_offsets;
  for (typename FormatStringType::const_iterator i = format_string.begin();
       i != format_string.end(); ++i) {
    if ('$' == *i) {
      if (i + 1 != format_string.end()) {
        ++i;
        DCHECK('$' == *i || '1' <= *i) << "Invalid placeholder: " << *i;
        if ('$' == *i) {
          while (i != format_string.end() && '$' == *i) {
            formatted.push_back('$');
            ++i;
          }
          --i;
        } else {
          uintptr_t index = 0;
          while (i != format_string.end() && '0' <= *i && *i <= '9') {
            index *= 10;
            index += *i - '0';
            ++i;
          }
          --i;
          index -= 1;
          if (offsets) {
            ReplacementOffset r_offset(index,
                static_cast<int>(formatted.size()));
            r_offsets.insert(std::lower_bound(r_offsets.begin(),
                                              r_offsets.end(),
                                              r_offset,
                                              &CompareParameter),
                             r_offset);
          }
          if (index < substitutions)
            formatted.append(subst.at(index));
        }
      }
    } else {
      formatted.push_back(*i);
    }
  }
  if (offsets) {
    for (std::vector<ReplacementOffset>::const_iterator i = r_offsets.begin();
         i != r_offsets.end(); ++i) {
      offsets->push_back(i->offset);
    }
  }
  return formatted;
}

string16 ReplaceStringPlaceholders(const string16& format_string,
                                   const std::vector<string16>& subst,
                                   std::vector<size_t>* offsets) {
  return DoReplaceStringPlaceholders(format_string, subst, offsets);
}

std::string ReplaceStringPlaceholders(const butil::StringPiece& format_string,
                                      const std::vector<std::string>& subst,
                                      std::vector<size_t>* offsets) {
  return DoReplaceStringPlaceholders(format_string, subst, offsets);
}

string16 ReplaceStringPlaceholders(const string16& format_string,
                                   const string16& a,
                                   size_t* offset) {
  std::vector<size_t> offsets;
  std::vector<string16> subst;
  subst.push_back(a);
  string16 result = ReplaceStringPlaceholders(format_string, subst, &offsets);

  DCHECK_EQ(1U, offsets.size());
  if (offset)
    *offset = offsets[0];
  return result;
}

static bool IsWildcard(base_icu::UChar32 character) {
  return character == '*' || character == '?';
}

// Move the strings pointers to the point where they start to differ.
template <typename CHAR, typename NEXT>
static void EatSameChars(const CHAR** pattern, const CHAR* pattern_end,
                         const CHAR** string, const CHAR* string_end,
                         NEXT next) {
  const CHAR* escape = NULL;
  while (*pattern != pattern_end && *string != string_end) {
    if (!escape && IsWildcard(**pattern)) {
      // We don't want to match wildcard here, except if it's escaped.
      return;
    }

    // Check if the escapement char is found. If so, skip it and move to the
    // next character.
    if (!escape && **pattern == '\\') {
      escape = *pattern;
      next(pattern, pattern_end);
      continue;
    }

    // Check if the chars match, if so, increment the ptrs.
    const CHAR* pattern_next = *pattern;
    const CHAR* string_next = *string;
    base_icu::UChar32 pattern_char = next(&pattern_next, pattern_end);
    if (pattern_char == next(&string_next, string_end) &&
        pattern_char != (base_icu::UChar32) CBU_SENTINEL) {
      *pattern = pattern_next;
      *string = string_next;
    } else {
      // Uh ho, it did not match, we are done. If the last char was an
      // escapement, that means that it was an error to advance the ptr here,
      // let's put it back where it was. This also mean that the MatchPattern
      // function will return false because if we can't match an escape char
      // here, then no one will.
      if (escape) {
        *pattern = escape;
      }
      return;
    }

    escape = NULL;
  }
}

template <typename CHAR, typename NEXT>
static void EatWildcard(const CHAR** pattern, const CHAR* end, NEXT next) {
  while (*pattern != end) {
    if (!IsWildcard(**pattern))
      return;
    next(pattern, end);
  }
}

template <typename CHAR, typename NEXT>
static bool MatchPatternT(const CHAR* eval, const CHAR* eval_end,
                          const CHAR* pattern, const CHAR* pattern_end,
                          int depth,
                          NEXT next) {
  const int kMaxDepth = 16;
  if (depth > kMaxDepth)
    return false;

  // Eat all the matching chars.
  EatSameChars(&pattern, pattern_end, &eval, eval_end, next);

  // If the string is empty, then the pattern must be empty too, or contains
  // only wildcards.
  if (eval == eval_end) {
    EatWildcard(&pattern, pattern_end, next);
    return pattern == pattern_end;
  }

  // Pattern is empty but not string, this is not a match.
  if (pattern == pattern_end)
    return false;

  // If this is a question mark, then we need to compare the rest with
  // the current string or the string with one character eaten.
  const CHAR* next_pattern = pattern;
  next(&next_pattern, pattern_end);
  if (pattern[0] == '?') {
    if (MatchPatternT(eval, eval_end, next_pattern, pattern_end,
                      depth + 1, next))
      return true;
    const CHAR* next_eval = eval;
    next(&next_eval, eval_end);
    if (MatchPatternT(next_eval, eval_end, next_pattern, pattern_end,
                      depth + 1, next))
      return true;
  }

  // This is a *, try to match all the possible substrings with the remainder
  // of the pattern.
  if (pattern[0] == '*') {
    // Collapse duplicate wild cards (********** into *) so that the
    // method does not recurse unnecessarily. http://crbug.com/52839
    EatWildcard(&next_pattern, pattern_end, next);

    while (eval != eval_end) {
      if (MatchPatternT(eval, eval_end, next_pattern, pattern_end,
                        depth + 1, next))
        return true;
      eval++;
    }

    // We reached the end of the string, let see if the pattern contains only
    // wildcards.
    if (eval == eval_end) {
      EatWildcard(&pattern, pattern_end, next);
      if (pattern != pattern_end)
        return false;
      return true;
    }
  }

  return false;
}

struct NextCharUTF8 {
  base_icu::UChar32 operator()(const char** p, const char* end) {
    base_icu::UChar32 c;
    int offset = 0;
    CBU8_NEXT(*p, offset, end - *p, c);
    *p += offset;
    return c;
  }
};

struct NextCharUTF16 {
  base_icu::UChar32 operator()(const char16** p, const char16* end) {
    base_icu::UChar32 c;
    int offset = 0;
    CBU16_NEXT(*p, offset, end - *p, c);
    *p += offset;
    return c;
  }
};

bool MatchPattern(const butil::StringPiece& eval,
                  const butil::StringPiece& pattern) {
  return MatchPatternT(eval.data(), eval.data() + eval.size(),
                       pattern.data(), pattern.data() + pattern.size(),
                       0, NextCharUTF8());
}

bool MatchPattern(const string16& eval, const string16& pattern) {
  return MatchPatternT(eval.c_str(), eval.c_str() + eval.size(),
                       pattern.c_str(), pattern.c_str() + pattern.size(),
                       0, NextCharUTF16());
}

// The following code is compatible with the OpenBSD lcpy interface.  See:
//   http://www.gratisoft.us/todd/papers/strlcpy.html
//   ftp://ftp.openbsd.org/pub/OpenBSD/src/lib/libc/string/{wcs,str}lcpy.c

namespace {

template <typename CHAR>
size_t lcpyT(CHAR* dst, const CHAR* src, size_t dst_size) {
  for (size_t i = 0; i < dst_size; ++i) {
    if ((dst[i] = src[i]) == 0)  // We hit and copied the terminating NULL.
      return i;
  }

  // We were left off at dst_size.  We over copied 1 byte.  Null terminate.
  if (dst_size != 0)
    dst[dst_size - 1] = 0;

  // Count the rest of the |src|, and return it's length in characters.
  while (src[dst_size]) ++dst_size;
  return dst_size;
}

}  // namespace

size_t butil::strlcpy(char* dst, const char* src, size_t dst_size) {
  return lcpyT<char>(dst, src, dst_size);
}
size_t butil::wcslcpy(wchar_t* dst, const wchar_t* src, size_t dst_size) {
  return lcpyT<wchar_t>(dst, src, dst_size);
}
