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



#include <sstream>                  // std::stringstream
#include <iomanip>                  // std::setw
#include "brpc/grpc.h"
#include "brpc/errno.pb.h"
#include "brpc/http_status_code.h"
#include "butil/logging.h"

namespace brpc {

const char* GrpcStatusToString(GrpcStatus s) {
    switch (s) {
        case GRPC_OK: return "GRPC_OK";
        case GRPC_CANCELED: return "GRPC_CANCELED";
        case GRPC_UNKNOWN: return "GRPC_UNKNOWN";
        case GRPC_INVALIDARGUMENT: return "GRPC_INVALIDARGUMENT";
        case GRPC_DEADLINEEXCEEDED: return "GRPC_DEADLINEEXCEEDED";
        case GRPC_NOTFOUND: return "GRPC_NOTFOUND";
        case GRPC_ALREADYEXISTS: return "GRPC_ALREADYEXISTS";
        case GRPC_PERMISSIONDENIED: return "GRPC_PERMISSIONDENIED";
        case GRPC_RESOURCEEXHAUSTED: return "GRPC_RESOURCEEXHAUSTED";
        case GRPC_FAILEDPRECONDITION: return "GRPC_FAILEDPRECONDITION";
        case GRPC_ABORTED: return "GRPC_ABORTED";
        case GRPC_OUTOFRANGE: return "GRPC_OUTOFRANGE";
        case GRPC_UNIMPLEMENTED: return "GRPC_UNIMPLEMENTED";
        case GRPC_INTERNAL: return "GRPC_INTERNAL";
        case GRPC_UNAVAILABLE: return "GRPC_UNAVAILABLE";
        case GRPC_DATALOSS: return "GRPC_DATALOSS";
        case GRPC_UNAUTHENTICATED: return "GRPC_UNAUTHENTICATED";
        case GRPC_MAX: return "GRPC_MAX";
    }
    return "Unknown-GrpcStatus";
}

GrpcStatus ErrorCodeToGrpcStatus(int error_code) {
    switch (error_code) {
    case 0:
        return GRPC_OK;
    case ENOSERVICE:
    case ENOMETHOD:
        return GRPC_UNIMPLEMENTED;
    case ERPCAUTH:
        return GRPC_UNAUTHENTICATED;
    case EREQUEST:
    case EINVAL:
        return GRPC_INVALIDARGUMENT;
    case ELIMIT:
        return GRPC_RESOURCEEXHAUSTED;
    case ELOGOFF:
        return GRPC_UNAVAILABLE;
    case EPERM:
        return GRPC_PERMISSIONDENIED;
    case ERPCTIMEDOUT:
        return GRPC_DEADLINEEXCEEDED;
    case ETIMEDOUT:
        return GRPC_INTERNAL;
    case ECANCELED:
        return GRPC_CANCELED;
    default:
        return GRPC_INTERNAL;
    }
}

int GrpcStatusToErrorCode(GrpcStatus grpc_status) {
    switch (grpc_status) {
    case GRPC_OK:
        return 0;
    case GRPC_CANCELED:
        return ECANCELED;
    case GRPC_UNKNOWN:
        return EINTERNAL;
    case GRPC_INVALIDARGUMENT:
        return EINVAL;
    case GRPC_DEADLINEEXCEEDED:
        return ERPCTIMEDOUT;
    case GRPC_NOTFOUND:
        return EINTERNAL;
    case GRPC_ALREADYEXISTS:
        return EEXIST;
    case GRPC_PERMISSIONDENIED:
        return EPERM;
    case GRPC_RESOURCEEXHAUSTED:
        return ELIMIT;
    case GRPC_FAILEDPRECONDITION:
    case GRPC_ABORTED:
    case GRPC_OUTOFRANGE:
         return EINTERNAL;
    case GRPC_UNIMPLEMENTED:
         return ENOMETHOD;
    case GRPC_INTERNAL:
    case GRPC_UNAVAILABLE:
         return EINTERNAL;
    case GRPC_DATALOSS:
         return EINTERNAL;
    case GRPC_UNAUTHENTICATED:
         return ERPCAUTH;
    default:
         return EINTERNAL;
    }
}

void PercentEncode(const std::string& str, std::string* str_out) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (std::string::const_iterator it = str.begin();
         it != str.end(); ++it) {
        const std::string::value_type& c = *it;
        // Unreserved Characters are referred from
        // https://en.wikipedia.org/wiki/Percent-encoding
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
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

void PercentDecode(const std::string& str, std::string* str_out) {
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

int64_t ConvertGrpcTimeoutToUS(const std::string* grpc_timeout) {
    if (!grpc_timeout || grpc_timeout->empty()) {
        return -1;
    }
    char* endptr = NULL;
    int64_t timeout_value = (int64_t)strtol(grpc_timeout->data(), &endptr, 10);
    // Only the format that the digit number is equal to (timeout header size - 1)
    // is valid. Otherwise the format is not valid and is treated as no deadline.
    // For example:
    //      "1H", "2993S", "82m" is valid.
    //      "30A" is also valid, but the following switch would fall into default
    //          case and return -1 since 'A' is not a valid time unit.
    //      "123ASH" is not vaid since the digit number is 3, while the size is 6.
    //      "HHH" is not valid since the dight number is 0, while the size is 3.
    if ((size_t)(endptr - grpc_timeout->data()) != grpc_timeout->size() - 1) {
        return -1;
    }
    switch (*endptr) {
        case 'H':
            return timeout_value * 3600 * 1000000;
        case 'M':
            return timeout_value * 60 * 1000000;
        case 'S':
            return timeout_value * 1000000;
        case 'm':
            return timeout_value * 1000;
        case 'u':
            return timeout_value;
        case 'n':
            timeout_value = (timeout_value + 500) / 1000;
            return (timeout_value == 0) ? 1 : timeout_value;
        default:
            return -1;
    }
    CHECK(false) << "Impossible";
}

} // namespace brpc
