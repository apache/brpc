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

#include "butil/bson_util.h"

#include <cassert>

#include "butil/logging.h"

static ssize_t BsonReaderWrapper(void* handle, void* buf, size_t count) {
    butil::IOBuf* iobuf = static_cast<butil::IOBuf*>(handle);
    return iobuf->cutn(buf, count);
}

namespace butil {
namespace bson {

BsonEnumerator::BsonEnumerator(IOBuf* iobuf)
    : _buf(iobuf)
    , _reader(::bson_reader_new_from_handle(_buf, BsonReaderWrapper, NULL)) { 
    if (!_reader) {
        _has_error = true;
    }
}

bool BsonEnumerator::HasError() {
    return _has_error;
}

const bson_t* BsonEnumerator::Next() {
    if (_has_error) {
        return nullptr;
    }

    bool eof;
    const bson_t* doc = bson_reader_read(_reader, &eof);
    if (!doc && !eof) {
        _has_error = true;
    }
    return doc;
}

BsonEnumerator::~BsonEnumerator() {
    if (_reader) {
        bson_reader_destroy(_reader);
    }
}

UniqueBsonPtr ExtractBsonFromIOBuf(IOBuf& iobuf) {
    uint32_t bson_length;
    const size_t n = iobuf.copy_to(&bson_length, sizeof(bson_length));
    if (n < sizeof(bson_length) || iobuf.size() < bson_length) {
        return nullptr;
    }
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[bson_length]);
    iobuf.copy_to(buffer.get(), bson_length);
    return UniqueBsonPtr(bson_new_from_data(buffer.get(), bson_length));
}

bool bson_get_double(bson_t* doc, const char *key, double *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_DOUBLE(&iter)) {
        return false;
    }
    *value = bson_iter_double(&iter);
    return true;
}

bool bson_get_int32(bson_t* doc, const char *key, int32_t *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_INT32(&iter)) {
        return false;
    }
    *value = bson_iter_int32(&iter);
    return true;
}

bool bson_get_int64(bson_t* doc, const char *key, int64_t *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_INT64(&iter)) {
        return false;
    }
    *value = bson_iter_int64(&iter);
    return true;
}

bool bson_get_str(bson_t* doc, const char *key, std::string *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_UTF8(&iter)) {
        return false;
    }
    uint32_t length = 0;
    const char *str = bson_iter_utf8(&iter, &length);
    if (!str) {
        return false;
    } else {
        *value = std::string(str, length);
        return true;
    }
}

bool bson_get_doc(bson_t* doc, const char *key, bson_t* *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_DOCUMENT(&iter)) {
        return false;
    }
    uint32_t length = 0;
    const uint8_t *document_str = nullptr;
    bson_iter_document(&iter, &length, &document_str);
    bson_t *value_doc = bson_new_from_data(document_str, length);
    if (!value_doc) {
        return false;
    }
    *value = value_doc;
    return true;
}

bool bson_get_array(bson_t* doc, const char *key, std::vector<bson_t*> *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_ARRAY(&iter)) {
        return false;
    }
    uint32_t length = 0;
    const uint8_t *array_str = nullptr;
    bson_iter_array(&iter, &length, &array_str);
    bson_t bson_array;  // read only
    bool array_init = bson_init_static(&bson_array, array_str, length);
    if (!array_init) {
        return false;
    }
    bson_iter_t array_iter;
    bool r = bson_iter_init(&array_iter, &bson_array);
    if (!r) {
        return false;
    }
    while (bson_iter_next(&array_iter)) {
        if (!BSON_ITER_HOLDS_DOCUMENT(&array_iter)) {
            continue;
        }
        uint32_t doc_length = 0;
        const uint8_t *document_str = nullptr;
        bson_iter_document(&array_iter, &doc_length, &document_str);
        bson_t *array_element_ptr = bson_new_from_data(document_str, doc_length);
        if (!array_element_ptr) {
            return false;
        }
        value->push_back(array_element_ptr);
    }
    return true;
}

bool bson_has_oid(bson_t* doc) {
    assert(doc);
    bson_iter_t iter;
    const char *oid = "_id";
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, oid) ||
        !BSON_ITER_HOLDS_OID(&iter)) {
        return false;
    }
    return true;
}

bool bson_get_oid(bson_t* doc, const char *key, bson_oid_t *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_OID(&iter)) {
        return false;
    }
    const bson_oid_t *oid = bson_iter_oid(&iter);
    if (!oid) {
        return false;
    } else {
        *value = *oid;
        return true;
    }
}

bool bson_get_bool(bson_t* doc, const char *key, bool *value) {
    assert(doc);
    assert(key);
    assert(value);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key) ||
        !BSON_ITER_HOLDS_BOOL(&iter)) {
        return false;
    }
    *value = bson_iter_bool(&iter);
    return true;
}

std::pair<bool, bson_type_t> bson_get_type(bson_t* doc, const char *key) {
    assert(doc);
    assert(key);
    bson_iter_t iter;
    if (!bson_iter_init(&iter, doc) || !bson_iter_find(&iter, key)) {
        return std::make_pair(false, BSON_TYPE_EOD);
    } else {
        return std::make_pair(true, bson_iter_type(&iter));
    }
}

}  // namespace bson
}  // namespace butil
