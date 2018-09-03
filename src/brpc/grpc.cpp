// Copyright (c) 2018 Bilibili, Inc.
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

// Authors: Jiashun Zhu(zhujiashun@bilibili.com)


#include <sstream>                  // std::stringstream
#include <iomanip>                  // std::setw
#include "brpc/grpc.h"
#include "brpc/errno.pb.h"
#include "brpc/http_status_code.h"
#include "butil/logging.h"

namespace brpc {

// The mapping can be found in grpc-go internal/transport/http_util.go
GrpcStatus HttpStatus2GrpcStatus(int http_status) {
    switch(http_status) {
        case HTTP_STATUS_BAD_REQUEST:
            return GRPC_INTERNAL;
        case HTTP_STATUS_UNAUTHORIZED:
            return GRPC_UNAUTHENTICATED;
        case HTTP_STATUS_FORBIDDEN:
            return GRPC_PERMISSIONDENIED;
        case HTTP_STATUS_NOT_FOUND:
            return GRPC_UNIMPLEMENTED;
        case HTTP_STATUS_BAD_GATEWAY:
        case HTTP_STATUS_SERVICE_UNAVAILABLE:
        case HTTP_STATUS_GATEWAY_TIMEOUT:
            return GRPC_UNAVAILABLE;
        default:
            return GRPC_UNKNOWN;
    }
}

GrpcStatus ErrorCode2GrpcStatus(int error_code) {
    switch (error_code) {
    case ENOSERVICE:
    case ENOMETHOD:
        return GRPC_UNIMPLEMENTED;
    case ERPCAUTH:
        return GRPC_UNAUTHENTICATED;
    case EREQUEST:
    case EINVAL:
        return GRPC_INVALIDARGUMENT;
    case ELIMIT:
    case ELOGOFF:
        return GRPC_UNAVAILABLE;
    case EPERM:
        return GRPC_PERMISSIONDENIED;
    case ERPCTIMEDOUT:
    case ETIMEDOUT:
        return GRPC_INTERNAL;
    default:
        return GRPC_UNKNOWN;
    }
}

void percent_encode(const std::string& str, std::string* str_out) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (std::string::const_iterator it = str.begin();
         it != str.end(); ++it) {
        const std::string::value_type& c = *it;
        if (c >= ' ' && c <= '~' && c != '%') {
            escaped << c;
            continue;
        }
        escaped << '%' << std::setw(2) << int((unsigned char) c);
    }
    if (str_out) {
        *str_out = escaped.str();
    }
}

static int hex_to_int(char c) {
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return 0;
}

void percent_decode(const std::string& str, std::string* str_out) {
    std::ostringstream unescaped;
    for (std::string::const_iterator it = str.begin();
         it != str.end(); ++it) {
        const std::string::value_type& c = *it;
        if (c == '%' && it + 2 < str.end()) {
            int i1 = hex_to_int(*++it);
            int i2 = hex_to_int(*++it);
            unescaped << (char)(i1 * 16 + i2);
        } else {
            unescaped << c;
        } 
    }
    if (str_out) {
        *str_out = unescaped.str();
    }
}

} // namespace brpc
