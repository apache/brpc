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

#ifndef BRPC_ADAPTIVE_MAX_CONCURRENCY_H
#define BRPC_ADAPTIVE_MAX_CONCURRENCY_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "butil/strings/string_piece.h"
#include "brpc/options.pb.h"

namespace brpc {

// timeout concurrency limiter config
struct TimeoutConcurrencyConf {
    int64_t timeout_ms;
    int max_concurrency;
};

class AdaptiveMaxConcurrency{
public:
    explicit AdaptiveMaxConcurrency();
    explicit AdaptiveMaxConcurrency(int max_concurrency);
    explicit AdaptiveMaxConcurrency(const butil::StringPiece& value);
    explicit AdaptiveMaxConcurrency(const TimeoutConcurrencyConf& value);

    // Non-trivial destructor to prevent AdaptiveMaxConcurrency from being
    // passed to variadic arguments without explicit type conversion.
    // eg:
    // printf("%d", options.max_concurrency)                  // compile error
    // printf("%s", options.max_concurrency.value().c_str()) // ok
    ~AdaptiveMaxConcurrency() {}

    void operator=(int max_concurrency);
    void operator=(const butil::StringPiece& value);
    void operator=(const TimeoutConcurrencyConf& value);

    // 0  for type="unlimited"
    // >0 for type="constant"
    // <0 for type="user-defined"
    operator int() const { return _max_concurrency; }
    operator TimeoutConcurrencyConf() const { return _timeout_conf; }

    // "unlimited" for type="unlimited"
    // "10" "20" "30" for type="constant"
    // "user-defined" for type="user-defined"
    const std::string& value() const { return _value; }

    // "unlimited", "constant" or "user-defined"
    const std::string& type() const;

    // Get strings filled with "unlimited" and "constant"
    static const std::string& UNLIMITED();
    static const std::string& CONSTANT();

private:
    std::string _value;
    int _max_concurrency;
    TimeoutConcurrencyConf
        _timeout_conf;  // TODO std::varient for different type
};

inline std::ostream& operator<<(std::ostream& os, const AdaptiveMaxConcurrency& amc) {
    return os << amc.value();
}

bool operator==(const AdaptiveMaxConcurrency& adaptive_concurrency,
                       const butil::StringPiece& concurrency);

inline bool operator==(const butil::StringPiece& concurrency,
                       const AdaptiveMaxConcurrency& adaptive_concurrency) {
    return adaptive_concurrency == concurrency;
}

inline bool operator!=(const AdaptiveMaxConcurrency& adaptive_concurrency,
                       const butil::StringPiece& concurrency) {
    return !(adaptive_concurrency == concurrency);
}

inline bool operator!=(const butil::StringPiece& concurrency,
                  const AdaptiveMaxConcurrency& adaptive_concurrency) {
    return !(adaptive_concurrency == concurrency);
}

}  // namespace brpc


#endif // BRPC_ADAPTIVE_MAX_CONCURRENCY_H
