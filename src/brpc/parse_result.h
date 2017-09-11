// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_PARSE_RESULT_H
#define BRPC_PARSE_RESULT_H


namespace brpc {

enum ParseError {
    PARSE_OK = 0,
    PARSE_ERROR_TRY_OTHERS,
    PARSE_ERROR_NOT_ENOUGH_DATA,
    PARSE_ERROR_TOO_BIG_DATA,
    PARSE_ERROR_NO_RESOURCE,
    PARSE_ERROR_ABSOLUTELY_WRONG,
};

inline const char* ParseErrorToString(ParseError e) {
    switch (e) {
    case PARSE_OK: return "ok";
    case PARSE_ERROR_TRY_OTHERS: return "try other protocols";
    case PARSE_ERROR_NOT_ENOUGH_DATA: return "not enough data";
    case PARSE_ERROR_TOO_BIG_DATA: return "too big data";
    case PARSE_ERROR_NO_RESOURCE: return "no resource for the message";
    case PARSE_ERROR_ABSOLUTELY_WRONG: return "absolutely wrong message";
    }
    return "unknown ParseError";
}

class InputMessageBase;

// A specialized Maybe<> type to represent a parsing result.
class ParseResult {
public:
    // Create a failed parsing result.
    explicit ParseResult(ParseError err)
        : _msg(NULL), _err(err), _user_desc(NULL) {}
    // The `user_desc' must be string constant or always valid.
    explicit ParseResult(ParseError err, const char* user_desc)
        : _msg(NULL), _err(err), _user_desc(user_desc) {}
    // Create a successful parsing result.
    explicit ParseResult(InputMessageBase* msg)
        : _msg(msg), _err(PARSE_OK), _user_desc(NULL) {}
    
    // Return PARSE_OK when the result is successful.
    ParseError error() const { return _err; }
    const char* error_str() const
    { return _user_desc ? _user_desc : ParseErrorToString(_err); }
    bool is_ok() const { return error() == PARSE_OK; }

    // definitely NULL when result is failed.
    InputMessageBase* message() const { return _msg; }
 
private:
    InputMessageBase* _msg;
    ParseError _err;
    const char* _user_desc;
};

// Wrap ParseError/message into ParseResult.
// You can also call ctor of ParseError directly.
inline ParseResult MakeParseError(ParseError err) {
    return ParseResult(err);
}
// The `user_desc' must be string constant or always valid.
inline ParseResult MakeParseError(ParseError err, const char* user_desc) {
    return ParseResult(err, user_desc);
}
inline ParseResult MakeMessage(InputMessageBase* msg) {
    return ParseResult(msg);
}

} // namespace brpc


#endif  // BRPC_PARSE_RESULT_H
