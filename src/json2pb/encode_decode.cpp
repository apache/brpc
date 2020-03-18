// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <string>
#include <sstream>
#include "encode_decode.h"

namespace json2pb {

inline int match_pattern(const std::string& str, int index) {
    //pattern: _Zxxx_  
    const char digit = '0';
    const int pattern_length = 6;

    int length = str.size();
    if (length <= index || length - index < pattern_length) {
        return -1;
    }
    if (str[index] != '_' ||
        str[index + 1] != 'Z' ||
        str[index + 5] != '_' || 
        !isdigit(str[index + 2]) ||
        !isdigit(str[index + 3]) ||
        !isdigit(str[index + 4])) { 
        return -1;
    }
    int sum = (str[index + 2] - digit) * 100 + (str[index + 3] - digit) * 10 + (str[index + 4] - digit);
    return sum < 256 ? sum : -1;
}

bool encode_name(const std::string& content, std::string& encoded_content) {
    int index = 0;
    size_t begin = 0;
    bool convert = false; 
    for (std::string::const_iterator it = content.begin(); it != content.end(); ++it, ++index) {
        if ((!isalnum(*it) && 
            (*it != '_')) ||
            (it == content.begin() &&
            isdigit(*it))) { 
            if (!convert) {
                encoded_content.clear();
                encoded_content.reserve(2*content.size());
                convert = true;
            }
            encoded_content.append(content, begin, index - begin);
            begin = index + 1;
            
            char pattern[6];
            pattern[0] = '_';
            pattern[1] = 'Z';
            int first = *it / 100;
            int second = (*it - first * 100) / 10;
            int third = *it - first * 100 - second * 10;
            pattern[2] = first + '0';
            pattern[3] = second + '0';
            pattern[4] = third + '0';
            pattern[5] = '_';
            encoded_content.append(pattern, sizeof(pattern));
        }
    }
    if (!convert) {
        return false;
    } else {
        encoded_content.append(content, begin, index - begin);
        return true;
    }
}

bool decode_name(const std::string& content, std::string& decoded_content) {
    const int pattern_length = 6;
    int begin = 0;
    int second = 0;
    bool convert = false; 
    for (std::string::const_iterator it = content.begin(); it < content.end(); ++it, ++second) {
        if (*it != '_') {
            continue;
        }
        int val = match_pattern(content, second);
        if (val != -1) { 
            if (!convert) {
                decoded_content.clear();
                decoded_content.reserve(content.size());
                convert = true;
            }
            decoded_content.append(content, begin, second - begin);
            decoded_content.push_back(static_cast<char>(val));
            second += pattern_length - 1;
            begin = second + 1;
            it += pattern_length - 1;
        }
    } 
    if (!convert) {
        return false;
    } else {
        decoded_content.append(content, begin, second - begin);
        return true; 
    }
} 

} // namespace json2pb
