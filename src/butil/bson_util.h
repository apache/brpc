#ifndef BUTIL_BSON_UTIL_H_
#define BUTIL_BSON_UTIL_H_

#include <bson/bson.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

namespace butil {
namespace bson {

typedef std::shared_ptr<bson_t> BsonPtr;

BsonPtr new_bson(bson_t *doc = nullptr);

bool bson_get_double(BsonPtr doc, const char *key, double *value);

bool bson_get_int32(BsonPtr doc, const char *key, int32_t *value);

bool bson_get_int64(BsonPtr doc, const char *key, int64_t *value);

bool bson_get_str(BsonPtr doc, const char *key, std::string *value);

bool bson_get_doc(BsonPtr doc, const char *key, BsonPtr *value);

bool bson_get_array(BsonPtr doc, const char *key, std::vector<BsonPtr> *value);

bool bson_has_oid(BsonPtr doc);

bool bson_get_oid(BsonPtr doc, const char *key, bson_oid_t *value);

bool bson_get_bool(BsonPtr doc, const char *key, bool *value);

std::pair<bool, bson_type_t> bson_get_type(BsonPtr doc, const char *key);
}  // namespace bson
}  // namespace butil

#endif  // BUTIL_BSON_UTIL_H_
