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

#include "brpc/checksum.h"

#include "brpc/protocol.h"
#include "butil/logging.h"

namespace brpc {

static const int MAX_HANDLER_SIZE = 1024;
static ChecksumHandler s_handler_map[MAX_HANDLER_SIZE] = {{NULL, NULL, NULL}};

int RegisterChecksumHandler(ChecksumType type, ChecksumHandler handler) {
    if (NULL == handler.Compute) {
        LOG(FATAL) << "Invalid parameter: handler function is NULL";
        return -1;
    }
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(FATAL) << "ChecksumType=" << type << " is out of range";
        return -1;
    }
    if (s_handler_map[index].Compute != NULL) {
        LOG(FATAL) << "ChecksumType=" << type << " was registered";
        return -1;
    }
    s_handler_map[index] = handler;
    return 0;
}

// Find ChecksumHandler by type.
// Returns NULL if not found
inline const ChecksumHandler* FindChecksumHandler(ChecksumType type) {
    int index = type;
    if (index < 0 || index >= MAX_HANDLER_SIZE) {
        LOG(ERROR) << "ChecksumType=" << type << " is out of range";
        return NULL;
    }
    if (NULL == s_handler_map[index].Compute) {
        return NULL;
    }
    return &s_handler_map[index];
}

const char* ChecksumTypeToCStr(ChecksumType type) {
    if (type == CHECKSUM_TYPE_NONE) {
        return "none";
    }
    const ChecksumHandler* handler = FindChecksumHandler(type);
    return (handler != NULL ? handler->name : "unknown");
}

void ListChecksumHandler(std::vector<ChecksumHandler>* vec) {
    vec->clear();
    for (int i = 0; i < MAX_HANDLER_SIZE; ++i) {
        if (s_handler_map[i].Compute != NULL) {
            vec->push_back(s_handler_map[i]);
        }
    }
}

// Compute `data' checksum
void ComputeDataChecksum(const ChecksumIn& in, ChecksumType checksum_type) {
    if (checksum_type == CHECKSUM_TYPE_NONE) {
        return;
    }
    const ChecksumHandler* handler = FindChecksumHandler(checksum_type);
    if (NULL != handler) {
        handler->Compute(in);
    }
}

// Verify `data' checksum Returns true on success, false otherwise
bool VerifyDataChecksum(const ChecksumIn& in, ChecksumType checksum_type) {
    if (checksum_type == CHECKSUM_TYPE_NONE) {
        return true;
    }
    const ChecksumHandler* handler = FindChecksumHandler(checksum_type);
    if (NULL != handler) {
        return handler->Verify(in);
    }
    return true;
}

}  // namespace brpc
