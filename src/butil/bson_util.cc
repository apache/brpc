#include "butil/bson_util.h"

#include <cassert>

#include "butil/logging.h"

namespace butil {
namespace bson {

BsonPtr new_bson(bson_t *doc) {
  if (!doc) {
    doc = bson_new();
  }
  return std::shared_ptr<bson_t>(doc, bson_destroy);
}

bool bson_get_double(BsonPtr doc, const char *key, double *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
      !BSON_ITER_HOLDS_DOUBLE(&iter)) {
    return false;
  }
  *value = bson_iter_double(&iter);
  return true;
}

bool bson_get_int32(BsonPtr doc, const char *key, int32_t *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
      !BSON_ITER_HOLDS_INT32(&iter)) {
    return false;
  }
  *value = bson_iter_int32(&iter);
  return true;
}

bool bson_get_int64(BsonPtr doc, const char *key, int64_t *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
      !BSON_ITER_HOLDS_INT64(&iter)) {
    return false;
  }
  *value = bson_iter_int64(&iter);
  return true;
}

bool bson_get_str(BsonPtr doc, const char *key, std::string *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
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

bool bson_get_doc(BsonPtr doc, const char *key, BsonPtr *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
      !BSON_ITER_HOLDS_DOCUMENT(&iter)) {
    return false;
  }
  uint32_t length = 0;
  const uint8_t *document_str = nullptr;
  bson_iter_document(&iter, &length, &document_str);
  bson_t *value_doc_ptr = bson_new_from_data(document_str, length);
  if (!value_doc_ptr) {
    return false;
  }
  *value = new_bson(value_doc_ptr);
  return true;
}

bool bson_get_array(BsonPtr doc, const char *key, std::vector<BsonPtr> *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
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
    value->push_back(new_bson(array_element_ptr));
  }
  return true;
}

bool bson_has_oid(BsonPtr doc) {
  assert(doc);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  const char *oid = "_id";
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, oid) ||
      !BSON_ITER_HOLDS_OID(&iter)) {
    return false;
  }
  return true;
}

bool bson_get_oid(BsonPtr doc, const char *key, bson_oid_t *value) {
  assert(doc);
  assert(key);
  assert(value);
  bson_iter_t iter;
  bson_t *doc_ptr = doc.get();
  if (!bson_iter_init(&iter, doc_ptr) || !bson_iter_find(&iter, key) ||
      !BSON_ITER_HOLDS_OID(&iter)) {
    return false;
  }
  uint32_t length = 0;
  const bson_oid_t *oid = bson_iter_oid(&iter);
  if (!oid) {
    return false;
  } else {
    *value = *oid;
    return true;
  }
}

}  // namespace bson
}  // namespace butil