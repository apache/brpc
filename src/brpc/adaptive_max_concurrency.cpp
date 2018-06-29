// Copyright (c) 2014 Baidu, Inc.G
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
//
// Authors: Lei He (helei@qiyi.com)

#include <cstring>
#include <strings.h>
#include "butil/logging.h"
#include "butil/strings/string_number_conversions.h"
#include "brpc/adaptive_max_concurrency.h"

namespace brpc {

inline bool CompareStringPieceWithoutCase(
        const butil::StringPiece& s1, const char* s2) {
    DCHECK(s2 != NULL);
    if (std::strlen(s2) != s1.size()) {
        return false;
    }
    return ::strncasecmp(s1.data(), s2, s1.size()) == 0;
}

AdaptiveMaxConcurrency::AdaptiveMaxConcurrency(const butil::StringPiece& name) {
    if (butil::StringToInt(name, &_max_concurrency) && _max_concurrency >= 0) {
        _name = "constant";
    } else if (_max_concurrency < 0) {
        LOG(FATAL) << "Invalid max_concurrency: " << name;
    } else {
        _name.assign(name.begin(), name.end());
        _max_concurrency = 0;
    }
}

void AdaptiveMaxConcurrency::operator=(const butil::StringPiece& name) {
    int max_concurrency = 0;
    if (butil::StringToInt(name, &max_concurrency) && max_concurrency >= 0) {
        _name = "constant";
        _max_concurrency = max_concurrency; 
    } else if (max_concurrency < 0) {
        LOG(ERROR) << "Fail to set max_concurrency, invalid value:" << name;
    } else if (CompareStringPieceWithoutCase(name, "constant")) {
        LOG(WARNING) 
            << "If you want to use a constant maximum concurrency, assign "
            << "an integer value directly to ServerOptions.max_concurrency "
            << "like: `server_options.max_concurrency = 1000`";
        _name.assign(name.begin(), name.end());
        _max_concurrency = 0;
    } else {
        _name.assign(name.begin(), name.end());
        _max_concurrency = 0;
    }
}

bool operator==(const AdaptiveMaxConcurrency& adaptive_concurrency,
                const butil::StringPiece& concurrency) {
    return CompareStringPieceWithoutCase(concurrency, 
                                         adaptive_concurrency.name().c_str());
}

}  // namespace brpc
