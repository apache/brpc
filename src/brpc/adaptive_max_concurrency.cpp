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

#include <cstring>
#include <strings.h>
#include "butil/string_printf.h"
#include "butil/logging.h"
#include "butil/strings/string_number_conversions.h"
#include "brpc/adaptive_max_concurrency.h"

namespace brpc {

AdaptiveMaxConcurrency::AdaptiveMaxConcurrency()
    : _value(UNLIMITED())
    , _max_concurrency(0) {
}

AdaptiveMaxConcurrency::AdaptiveMaxConcurrency(int max_concurrency)
    : _max_concurrency(0) {
    if (max_concurrency <= 0) {
        _value = UNLIMITED();
        _max_concurrency = 0;
    } else {
        _value = butil::string_printf("%d", max_concurrency);
        _max_concurrency = max_concurrency;
    }
}

AdaptiveMaxConcurrency::AdaptiveMaxConcurrency(
    const TimeoutConcurrencyConf& value)
    : _value("timeout"), _max_concurrency(-1), _timeout_conf(value) {}

inline bool CompareStringPieceWithoutCase(
    const butil::StringPiece& s1, const char* s2) {
    DCHECK(s2 != NULL);
    if (std::strlen(s2) != s1.size()) {
        return false;
    }
    return ::strncasecmp(s1.data(), s2, s1.size()) == 0;
}

AdaptiveMaxConcurrency::AdaptiveMaxConcurrency(const butil::StringPiece& value)
    : _max_concurrency(0) {
    int max_concurrency = 0;
    if (butil::StringToInt(value, &max_concurrency)) {
        operator=(max_concurrency);
    } else {
        value.CopyToString(&_value);
        _max_concurrency = -1;
    }
}

void AdaptiveMaxConcurrency::operator=(const butil::StringPiece& value) {
    int max_concurrency = 0;
    if (butil::StringToInt(value, &max_concurrency)) {
        return operator=(max_concurrency);
    } else {
        value.CopyToString(&_value);
        _max_concurrency = -1;
    }
}

void AdaptiveMaxConcurrency::operator=(int max_concurrency) {
    if (max_concurrency <= 0) {
        _value = UNLIMITED();
        _max_concurrency = 0;
    } else {
        _value = butil::string_printf("%d", max_concurrency);
        _max_concurrency = max_concurrency;
    }
}

void AdaptiveMaxConcurrency::operator=(const TimeoutConcurrencyConf& value) {
    _value = "timeout";
    _max_concurrency = -1;
    _timeout_conf = value;
}

const std::string& AdaptiveMaxConcurrency::type() const {
    if (_max_concurrency > 0) {
        return CONSTANT();
    } else if (_max_concurrency == 0) {
        return UNLIMITED();
    } else {
        return _value;
    }
}

const std::string& AdaptiveMaxConcurrency::UNLIMITED() {
    static std::string* s = new std::string("unlimited");
    return *s;
}

const std::string& AdaptiveMaxConcurrency::CONSTANT() {
    static std::string* s = new std::string("constant");
    return *s;
}

bool operator==(const AdaptiveMaxConcurrency& adaptive_concurrency,
                const butil::StringPiece& concurrency) {
    return CompareStringPieceWithoutCase(concurrency, 
                                         adaptive_concurrency.value().c_str());
}

}  // namespace brpc
