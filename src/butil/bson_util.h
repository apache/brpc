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

#ifndef BUTIL_BSON_UTIL_H
#define BUTIL_BSON_UTIL_H

#include <bson.h>

#include <stdint.h>
#include <memory>
#include <string>
#include <vector>

#include "butil/iobuf.h"

namespace butil {
namespace bson {

struct BsonDeleter {
    void operator()(bson_t* doc) {
        bson_destroy(doc);
    } 
};

class BsonEnumerator {
public:
    BsonEnumerator(IOBuf* iobuf);
    bool HasError();
    const bson_t* Next();
    ~BsonEnumerator();

private:
    bool _has_error;
    IOBuf* _buf;
    bson_reader_t* _reader;
};

typedef std::unique_ptr<bson_t, BsonDeleter> UniqueBsonPtr;

UniqueBsonPtr ExtractBsonFromIOBuf(IOBuf& iobuf);

bool bson_get_double(bson_t* doc, const char *key, double* value);

bool bson_get_int32(bson_t* doc, const char *key, int32_t *value);

bool bson_get_int64(bson_t* doc, const char *key, int64_t *value);

bool bson_get_str(bson_t* doc, const char *key, std::string *value);

bool bson_get_doc(bson_t* doc, const char *key, bson_t* *value);

bool bson_get_array(bson_t* doc, const char *key, std::vector<bson_t*> *value);

bool bson_has_oid(bson_t* doc);

bool bson_get_oid(bson_t* doc, const char *key, bson_oid_t *value);

bool bson_get_bool(bson_t* doc, const char *key, bool *value);

std::pair<bool, bson_type_t> bson_get_type(bson_t* doc, const char *key);

}  // namespace bson
}  // namespace butil

#endif  // BUTIL_BSON_UTIL_H
