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

#include "brpc/couchbase.h"

#include <zlib.h>  //for crc32 Vbucket_id

// Debug flag for enabling debug statements
static bool DBUG = false;  // Set to true to enable debug logs

// Debug print macro
#define DEBUG_PRINT(msg)                           \
  do {                                             \
    if (DBUG) {                                    \
      std::cout << "[DEBUG] " << msg << std::endl; \
    }                                              \
  } while (0)

#include <iostream>

#include "brpc/policy/couchbase_protocol.h"
#include "brpc/proto_base.pb.h"
#include "butil/logging.h"
#include "butil/macros.h"
#include "butil/string_printf.h"
#include "butil/sys_byteorder.h"
#include "butil/third_party/rapidjson/document.h"
#include "butil/third_party/rapidjson/rapidjson.h"

namespace brpc {

// Couchbase protocol constants
namespace {
[[maybe_unused]] constexpr uint32_t APPLE_VBUCKET_COUNT = 64;
constexpr uint32_t DEFAULT_VBUCKET_COUNT = 1024;
constexpr int CONNECTION_ID_SIZE = 33;
constexpr size_t RANDOM_ID_HEX_SIZE = 67;  // 33 bytes * 2 + null terminator
}  // namespace

// Static member definitions
CouchbaseManifestManager*
    CouchbaseOperations::CouchbaseRequest::metadata_tracking =
        &common_metadata_tracking;

bool brpc::CouchbaseManifestManager::setBucketToCollectionManifest(
    std::string server, std::string bucket,
    CouchbaseManifestManager::CollectionManifest manifest) {
  // Then update the collection manifest with proper locking
  {
    UniqueLock write_lock(rw_bucket_to_collection_manifest_mutex_);
    bucket_to_collection_manifest_[server][bucket] = manifest;
  }

  return true;
}

bool brpc::CouchbaseManifestManager::getBucketToCollectionManifest(
    std::string server, std::string bucket,
    CouchbaseManifestManager::CollectionManifest* manifest) {
  SharedLock read_lock(rw_bucket_to_collection_manifest_mutex_);
  auto it1 = bucket_to_collection_manifest_.find(server);
  if (it1 == bucket_to_collection_manifest_.end()) {
    return false;
  }
  auto it2 = it1->second.find(bucket);
  if (it2 == it1->second.end()) {
    return false;
  }
  *manifest = it2->second;
  return true;
}

bool brpc::CouchbaseManifestManager::getManifestToCollectionId(
    CouchbaseManifestManager::CollectionManifest* manifest, std::string scope,
    std::string collection, uint8_t* collection_id) {
  if (manifest == nullptr || collection_id == nullptr) {
    DEBUG_PRINT("Invalid input: manifest or collection_id is null");
    return false;
  }
  auto it1 = manifest->scope_to_collection_id_map.find(scope);
  if (it1 == manifest->scope_to_collection_id_map.end()) {
    DEBUG_PRINT("Scope: " << scope << " not found in manifest");
    return false;
  }
  auto it2 = it1->second.find(collection);
  if (it2 == it1->second.end()) {
    DEBUG_PRINT("Collection: " << collection
                               << " not found in scope: " << scope);
    return false;
  }
  *collection_id = it2->second;
  return true;
}

bool CouchbaseManifestManager::jsonToCollectionManifest(
    const std::string& json,
    CouchbaseManifestManager::CollectionManifest* manifest) {
  if (manifest == nullptr) {
    DEBUG_PRINT("Invalid input: manifest is null");
    return false;
  }

  // Clear existing data
  manifest->uid.clear();
  manifest->scope_to_collection_id_map.clear();

  if (json.empty()) {
    DEBUG_PRINT("JSON std::string is empty");
    return false;
  }

  // Parse JSON using RapidJSON
  BUTIL_RAPIDJSON_NAMESPACE::Document document;
  document.Parse(json.c_str());

  if (document.HasParseError()) {
    DEBUG_PRINT("Failed to parse JSON: " << document.GetParseError());
    return false;
  }

  if (!document.IsObject()) {
    DEBUG_PRINT("JSON root is not an object");
    return false;
  }

  // Extract uid
  if (document.HasMember("uid") && document["uid"].IsString()) {
    manifest->uid = document["uid"].GetString();
  } else {
    DEBUG_PRINT("Missing or invalid 'uid' field in JSON");
    return false;
  }

  // Extract scopes
  if (!document.HasMember("scopes") || !document["scopes"].IsArray()) {
    DEBUG_PRINT("Missing or invalid 'scopes' field in JSON");
    return false;
  }

  const BUTIL_RAPIDJSON_NAMESPACE::Value& scopes = document["scopes"];
  for (BUTIL_RAPIDJSON_NAMESPACE::SizeType i = 0; i < scopes.Size(); ++i) {
    const BUTIL_RAPIDJSON_NAMESPACE::Value& scope = scopes[i];

    if (!scope.IsObject()) {
      DEBUG_PRINT("Scope at index " << i << " is not an object");
      return false;
    }

    // Extract scope name
    if (!scope.HasMember("name") || !scope["name"].IsString()) {
      DEBUG_PRINT("Missing or invalid 'name' field in scope at index " << i);
      return false;
    }
    std::string scope_name = scope["name"].GetString();

    // Extract collections
    if (!scope.HasMember("collections") || !scope["collections"].IsArray()) {
      DEBUG_PRINT("Missing or invalid 'collections' field in scope '"
                  << scope_name << "'");
      return false;
    }

    const BUTIL_RAPIDJSON_NAMESPACE::Value& collections = scope["collections"];
    std:: unordered_map<std::string, uint8_t> collection_map;

    for (BUTIL_RAPIDJSON_NAMESPACE::SizeType j = 0; j < collections.Size();
         ++j) {
      const BUTIL_RAPIDJSON_NAMESPACE::Value& collection = collections[j];

      if (!collection.IsObject()) {
        DEBUG_PRINT("Collection at index " << j << " in scope '" << scope_name
                                           << "' is not an object");
        return false;
      }

      // Extract collection name
      if (!collection.HasMember("name") || !collection["name"].IsString()) {
        DEBUG_PRINT("Missing or invalid 'name' field in collection at index "
                    << j << " in scope '" << scope_name << "'");
        return false;
      }
      std::string collection_name = collection["name"].GetString();

      // Extract collection uid (hex std::string)
      if (!collection.HasMember("uid") || !collection["uid"].IsString()) {
        DEBUG_PRINT("Missing or invalid 'uid' field in collection '"
                    << collection_name << "' in scope '" << scope_name << "'");
        return false;
      }
      std::string collection_uid_str = collection["uid"].GetString();

      // Convert hex std::string to uint8_t
      uint8_t collection_id = 0;
      try {
        // Convert hex std::string to integer
        unsigned long uid_val = std::stoul(collection_uid_str, nullptr, 16);
        if (uid_val > 255) {
          DEBUG_PRINT(
              "Collection uid '"
              << collection_uid_str << "' exceeds uint8_t range in collection '"
              << collection_name << "' in scope '" << scope_name << "'");
          return false;
        }
        collection_id = static_cast<uint8_t>(uid_val);
      } catch (const std::exception& e) {
        DEBUG_PRINT("Failed to parse collection uid '"
                    << collection_uid_str << "' as hex in collection '"
                    << collection_name << "' in scope '" << scope_name << ": "
                    << e.what());
        return false;
      }

      // Add to collection map
      collection_map[collection_name] = collection_id;
    }

    // Add scope and its collections to manifest
    manifest->scope_to_collection_id_map[scope_name] =
        std::move(collection_map);
  }

  return true;
}

bool CouchbaseManifestManager::refreshCollectionManifest(
    brpc::Channel* channel, const std::string& server, const std::string& bucket,
    std:: unordered_map<std::string, CollectionManifest>*
        local_collection_manifest_cache) {
  // first fetch the manifest
  // then compare the UID with the cached one
  if (channel == nullptr) {
    DEBUG_PRINT("No channel found, make sure to call Authenticate() first");
    return false;
  }
  if (server.empty()) {
    DEBUG_PRINT("Server is empty, make sure to call Authenticate() first");
    return false;
  }
  if (bucket.empty()) {
    DEBUG_PRINT("No bucket selected, make sure to call SelectBucket() first");
    return false;
  }
  CouchbaseOperations::CouchbaseRequest temp_get_manifest_request;
  CouchbaseOperations::CouchbaseResponse temp_get_manifest_response;
  brpc::Controller temp_cntl;
  temp_get_manifest_request.getCollectionManifest();
  channel->CallMethod(NULL, &temp_cntl, &temp_get_manifest_request,
                      &temp_get_manifest_response, NULL);
  if (temp_cntl.Failed()) {
    DEBUG_PRINT("Failed to get collection manifest: bRPC controller error "
                << temp_cntl.ErrorText());
    return false;
  }
  std::string manifest_json;
  if (!temp_get_manifest_response.popManifest(&manifest_json)) {
    DEBUG_PRINT("Failed to parse response for refreshing collection Manifest: "
                << temp_get_manifest_response.lastError());
    return false;
  }
  brpc::CouchbaseManifestManager::CollectionManifest manifest;
  if (!common_metadata_tracking.jsonToCollectionManifest(manifest_json,
                                                         &manifest)) {
    DEBUG_PRINT("Failed to parse collection manifest JSON");
    return false;
  }
  brpc::CouchbaseManifestManager::CollectionManifest cached_manifest;
  if (!common_metadata_tracking.getBucketToCollectionManifest(
          server, bucket, &cached_manifest)) {
    // No cached manifest found, set the new one
    if (!common_metadata_tracking.setBucketToCollectionManifest(server, bucket,
                                                                manifest)) {
      DEBUG_PRINT("Failed to cache collection manifest for bucket "
                  << bucket << " on server " << server);
      return false;
    }
    DEBUG_PRINT("Cached collection manifest for bucket "
                << bucket << " on server " << server);
    // also update the local cache
    if (local_collection_manifest_cache != nullptr) {
      (*local_collection_manifest_cache)[bucket] = manifest;
    }
    return true;
  }
  // Compare the UID with the cached one
  // If they are different, refresh the cache
  else if (manifest.uid != cached_manifest.uid) {
    DEBUG_PRINT("Collection manifest has changed for bucket "
                << bucket << " on server " << server);
    if (!common_metadata_tracking.setBucketToCollectionManifest(server, bucket,
                                                                manifest)) {
      DEBUG_PRINT("Failed to update cached collection manifest for bucket "
                  << bucket << " on server " << server);
      return false;
    }
    DEBUG_PRINT("Updated cached collection manifest for bucket "
                << bucket << " on server " << server);
    // update the local cache as well
    if (local_collection_manifest_cache != nullptr) {
      (*local_collection_manifest_cache)[bucket] = manifest;
      DEBUG_PRINT("Added to local collection manifest cache for bucket "
                  << bucket << " on server " << server);
    }
    return true;
  } else {
    DEBUG_PRINT("Collection manifest is already up-to-date for bucket "
                << bucket << " on server " << server);
    if (local_collection_manifest_cache != nullptr) {
      if (local_collection_manifest_cache->find(bucket) !=
          local_collection_manifest_cache->end()) {
        // if the bucket already exists in the local cache, check the UID
        if ((*local_collection_manifest_cache)[bucket].uid != manifest.uid) {
          // if the UID is different, update the local cache
          (*local_collection_manifest_cache)[bucket] = manifest;
          DEBUG_PRINT("Updated local collection manifest cache for bucket "
                      << bucket << " on server " << server);
        }
      } else {
        // if the bucket does not exist in the local cache, add it
        (*local_collection_manifest_cache)[bucket] = manifest;
        DEBUG_PRINT("Added to local collection manifest cache for bucket "
                    << bucket << " on server " << server);
      }
    }
    return false;
  }
}

uint32_t CouchbaseOperations::CouchbaseRequest::hashCrc32(const char* key,
                                                          size_t key_length) {
  static const uint32_t crc32tab[256] = {
      0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
      0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
      0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
      0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
      0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
      0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
      0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
      0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
      0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
      0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
      0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
      0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
      0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
      0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
      0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
      0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
      0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
      0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
      0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
      0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
      0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
      0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
      0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
      0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
      0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
      0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
      0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
      0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
      0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
      0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
      0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
      0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
      0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
      0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
      0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
      0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
      0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
      0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
      0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
      0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
      0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
      0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
      0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d,
  };

  uint64_t x;
  uint32_t crc = UINT32_MAX;

  for (x = 0; x < key_length; x++)
    crc = (crc >> 8) ^ crc32tab[(crc ^ (uint64_t)key[x]) & 0xff];

#ifdef __APPLE__
  return ((~crc) >> 16) % APPLE_VBUCKET_COUNT;
#else
  return ((~crc) >> 16) % DEFAULT_VBUCKET_COUNT;
#endif
}

void CouchbaseOperations::CouchbaseRequest::sharedCtor() {
  _pipelined_count = 0;
  _cached_size_ = 0;
}

void CouchbaseOperations::CouchbaseRequest::sharedDtor() {}

void CouchbaseOperations::CouchbaseRequest::setCachedSize(int size) const {
  _cached_size_ = size;
}

void CouchbaseOperations::CouchbaseRequest::Clear() {
  _buf.clear();
  _pipelined_count = 0;
}

// Support for scope level collections will be added in future.
// Get the Scope ID for a given scope name
// bool CouchbaseOperations::CouchbaseRequest::GetScopeId(const
// butil::StringPiece& scope_name) {
//   if (scope_name.empty()) {
//     DEBUG_PRINT("Empty scope name");
//     return false;
//   }
//   // Opcode 0xBC for Get Scope ID (see Collections.md)
//   const policy::CouchbaseRequestHeader header = {
//       policy::CB_MAGIC_REQUEST,
//       policy::CB_GET_SCOPE_ID,
//       butil::HostToNet16(scope_name.size()),
//       0,  // no extras
//       policy::CB_BINARY_RAW_BYTES,
//       0,  // no vbucket
//       butil::HostToNet32(scope_name.size()),
//       0,  // opaque
//       0   // no CAS
//   };
//   if (_buf.append(&header, sizeof(header))) {
//     return false;
//   }
//   if (_buf.append(scope_name.data(), scope_name.size())) {
//     return false;
//   }
//   ++_pipelined_count;
//   return true;
// }

bool CouchbaseOperations::CouchbaseRequest::selectBucketRequest(
    const butil::StringPiece& bucket_name) {
  if (bucket_name.empty()) {
    DEBUG_PRINT("Empty bucket name");
    return false;
  }
  // construct the request header
  const policy::CouchbaseRequestHeader header = {
      policy::CB_MAGIC_REQUEST,
      policy::CB_SELECT_BUCKET,
      butil::HostToNet16(bucket_name.size()),
      0,
      policy::CB_BINARY_RAW_BYTES,
      0,
      butil::HostToNet32(bucket_name.size()),
      0,
      0};
  if (_buf.append(&header, sizeof(header))) {
    DEBUG_PRINT("Failed to append header to buffer");
    return false;
  }
  if (_buf.append(bucket_name.data(), bucket_name.size())) {
    DEBUG_PRINT("Failed to append bucket name to buffer");
    return false;
  }
  ++_pipelined_count;
  return true;
}

// HelloRequest sends a Hello request to the Couchbase server, which specifies
// the client features and capabilities.
//  This is typically the first request sent after connecting to the server.
//  It includes the agent name and a randomly generated connection ID in JSON
//  format.
bool CouchbaseOperations::CouchbaseRequest::helloRequest() {
  std::string agent = "brpc/1.0.0 (";
#ifdef __APPLE__
  agent += "Darwin/";
#elif defined(__linux__)
  agent += "Linux/";
#else
  agent += "UnknownOS/";
#endif
#if defined(__x86_64__)
  agent += "x86_64";
#elif defined(__aarch64__)
  agent += "arm64";
#else
  agent += "unknown";
#endif
  agent += ";bssl/0x1010107f)";

  // Generate a random connection ID as hex std::string
  unsigned char raw_id[CONNECTION_ID_SIZE];
  FILE* urandom = fopen("/dev/urandom", "rb");
  if (!urandom ||
      fread(raw_id, 1, CONNECTION_ID_SIZE, urandom) != CONNECTION_ID_SIZE) {
    if (urandom) fclose(urandom);
    DEBUG_PRINT("Failed to generate random connection id");
    return false;
  }
  fclose(urandom);
  char hex_id[RANDOM_ID_HEX_SIZE] = {0};
  for (int i = 0; i < CONNECTION_ID_SIZE; ++i) {
    sprintf(hex_id + i * 2, "%02x", raw_id[i]);
  }

  // Format key as JSON: {"a":"agent","i":"hex_id"}
  std::string key =
      std::string("{\"a\":\"") + agent + "\",\"i\":\"" + hex_id + "\"}";

  const uint16_t key_len = key.size();
  uint16_t features[] = {
      butil::HostToNet16(0x0001),  // Datatype
      butil::HostToNet16(0x0006),  // XError
      butil::HostToNet16(0x0007),  // SelectBucket
      butil::HostToNet16(0x000b),  // Snappy
      butil::HostToNet16(0x0012)   // Collections
  };

  const uint32_t value_len = sizeof(features);
  const uint32_t total_body_len = key_len + value_len;

  const policy::CouchbaseRequestHeader header = {
      policy::CB_MAGIC_REQUEST,
      policy::CB_HELLO_SELECT_FEATURES,
      butil::HostToNet16(key_len),         // key length
      0,                                   // extras length
      policy::CB_BINARY_RAW_BYTES,         // data type
      0,                                   // vbucket id
      butil::HostToNet32(total_body_len),  // total body length
      0,                                   // opaque
      0                                    // cas value
  };

  if (_buf.append(&header, sizeof(header))) {
    DEBUG_PRINT("Failed to append Hello header to buffer");
    return false;
  }
  if (_buf.append(key.data(), key_len)) {
    DEBUG_PRINT("Failed to append Hello JSON key to buffer");
    return false;
  }
  if (_buf.append(reinterpret_cast<const char*>(features), value_len)) {
    DEBUG_PRINT("Failed to append Hello features to buffer");
    return false;
  }
  ++_pipelined_count;
  return true;
}

bool CouchbaseOperations::CouchbaseRequest::authenticateRequest(
    const butil::StringPiece& username, const butil::StringPiece& password) {
  if (username.empty() || password.empty()) {
    DEBUG_PRINT("Empty username or password");
    return false;
  }
  // insert the features to get enabled, calling function helloRequest() will do
  // this.
  if (!helloRequest()) {
    DEBUG_PRINT("Failed to send helloRequest for authentication");
    return false;
  }
  // Construct the request header
  constexpr char kPlainAuthCommand[] = "PLAIN";
  constexpr char kPadding[1] = {'\0'};
  const brpc::policy::CouchbaseRequestHeader header = {
      brpc::policy::CB_MAGIC_REQUEST,
      brpc::policy::CB_BINARY_SASL_AUTH,
      butil::HostToNet16(sizeof(kPlainAuthCommand) - 1),
      0,
      0,
      0,
      butil::HostToNet32(sizeof(kPlainAuthCommand) + 1 + username.length() * 2 +
                         password.length()),
      0,
      0};
  std::string auth_str;
  auth_str.reserve(sizeof(header) + sizeof(kPlainAuthCommand) - 1 +
                   username.size() * 2 + password.size() + 2);
  auth_str.append(reinterpret_cast<const char*>(&header), sizeof(header));
  auth_str.append(kPlainAuthCommand, sizeof(kPlainAuthCommand) - 1);
  auth_str.append(username.data(), username.size());
  auth_str.append(kPadding, sizeof(kPadding));
  auth_str.append(username.data(), username.size());
  auth_str.append(kPadding, sizeof(kPadding));
  auth_str.append(password.data(), password.size());
  if (_buf.append(auth_str.data(), auth_str.size())) {
    DEBUG_PRINT("Failed to append auth std::string to buffer");
    return false;
  }
  ++_pipelined_count;
  return true;
}

void CouchbaseOperations::CouchbaseRequest::MergeFrom(
    const CouchbaseRequest& from) {
  CHECK_NE(&from, this);
  _buf.append(from._buf);
  _pipelined_count += from._pipelined_count;
}

bool CouchbaseOperations::CouchbaseRequest::IsInitialized() const {
  return _pipelined_count != 0;
}

void CouchbaseOperations::CouchbaseRequest::Swap(CouchbaseRequest* other) {
  if (other != this) {
    _buf.swap(other->_buf);
    std::swap(_pipelined_count, other->_pipelined_count);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

void CouchbaseOperations::CouchbaseResponse::sharedCtor() { _cached_size_ = 0; }

void CouchbaseOperations::CouchbaseResponse::sharedDtor() {}

void CouchbaseOperations::CouchbaseResponse::setCachedSize(int size) const {
  _cached_size_ = size;
}

void CouchbaseOperations::CouchbaseResponse::Clear() {}

void CouchbaseOperations::CouchbaseResponse::MergeFrom(
    const CouchbaseResponse& from) {
  CHECK_NE(&from, this);
  _err = from._err;
  _buf.append(from._buf);
}

bool CouchbaseOperations::CouchbaseResponse::IsInitialized() const {
  return !_buf.empty();
}

void CouchbaseOperations::CouchbaseResponse::swap(CouchbaseResponse* other) {
  if (other != this) {
    _buf.swap(other->_buf);
    std::swap(_cached_size_, other->_cached_size_);
  }
}

// ===================================================================

const char* CouchbaseOperations::CouchbaseResponse::statusStr(Status st) {
  switch (st) {
    case STATUS_SUCCESS:
      return "SUCCESS";
    case STATUS_KEY_ENOENT:
      return "Key not found";
    case STATUS_KEY_EEXISTS:
      return "Key already exists";
    case STATUS_E2BIG:
      return "Value too large";
    case STATUS_EINVAL:
      return "Invalid arguments";
    case STATUS_NOT_STORED:
      return "Item not stored";
    case STATUS_DELTA_BADVAL:
      return "Invalid delta value for increment/decrement";
    case STATUS_VBUCKET_BELONGS_TO_ANOTHER_SERVER:
      return "VBucket belongs to another server";
    case STATUS_AUTH_ERROR:
      return "Authentication failed";
    case STATUS_AUTH_CONTINUE:
      return "Authentication continue";
    case STATUS_ERANGE:
      return "Range error";
    case STATUS_ROLLBACK:
      return "Rollback required";
    case STATUS_EACCESS:
      return "Access denied";
    case STATUS_NOT_INITIALIZED:
      return "Not initialized";
    case STATUS_UNKNOWN_COMMAND:
      return "Unknown command";
    case STATUS_ENOMEM:
      return "Out of memory";
    case STATUS_NOT_SUPPORTED:
      return "Operation not supported";
    case STATUS_EINTERNAL:
      return "Internal server error";
    case STATUS_EBUSY:
      return "Server busy";
    case STATUS_ETMPFAIL:
      return "Temporary failure";
    case STATUS_UNKNOWN_COLLECTION:
      return "Unknown collection";
    case STATUS_NO_COLLECTIONS_MANIFEST:
      return "No collections manifest";
    case STATUS_CANNOT_APPLY_COLLECTIONS_MANIFEST:
      return "Cannot apply collections manifest";
    case STATUS_COLLECTIONS_MANIFEST_IS_AHEAD:
      return "Collections manifest is ahead";
    case STATUS_UNKNOWN_SCOPE:
      return "Unknown scope";
    case STATUS_DCP_STREAM_ID_INVALID:
      return "Invalid DCP stream ID";
    case STATUS_DURABILITY_INVALID_LEVEL:
      return "Invalid durability level";
    case STATUS_DURABILITY_IMPOSSIBLE:
      return "Durability requirements impossible";
    case STATUS_SYNC_WRITE_IN_PROGRESS:
      return "Synchronous write in progress";
    case STATUS_SYNC_WRITE_AMBIGUOUS:
      return "Synchronous write result ambiguous";
    case STATUS_SYNC_WRITE_RE_COMMIT_IN_PROGRESS:
      return "Synchronous write re-commit in progress";
    case STATUS_SUBDOC_PATH_NOT_FOUND:
      return "Sub-document path not found";
    case STATUS_SUBDOC_PATH_MISMATCH:
      return "Sub-document path mismatch";
    case STATUS_SUBDOC_PATH_EINVAL:
      return "Invalid sub-document path";
    case STATUS_SUBDOC_PATH_E2BIG:
      return "Sub-document path too deep";
    case STATUS_SUBDOC_DOC_E2DEEP:
      return "Sub-document too deep";
    case STATUS_SUBDOC_VALUE_CANTINSERT:
      return "Cannot insert sub-document value";
    case STATUS_SUBDOC_DOC_NOT_JSON:
      return "Document is not JSON";
    case STATUS_SUBDOC_NUM_E2BIG:
      return "Sub-document number too large";
    case STATUS_SUBDOC_DELTA_E2BIG:
      return "Sub-document delta too large";
    case STATUS_SUBDOC_PATH_EEXISTS:
      return "Sub-document path already exists";
    case STATUS_SUBDOC_VALUE_E2DEEP:
      return "Sub-document value too deep";
    case STATUS_SUBDOC_INVALID_COMBO:
      return "Invalid sub-document operation combination";
    case STATUS_SUBDOC_MULTI_PATH_FAILURE:
      return "Sub-document multi-path operation failed";
    case STATUS_SUBDOC_SUCCESS_DELETED:
      return "Sub-document operation succeeded on deleted document";
    case STATUS_SUBDOC_XATTR_INVALID_FLAG_COMBO:
      return "Invalid extended attribute flag combination";
    case STATUS_SUBDOC_XATTR_INVALID_KEY_COMBO:
      return "Invalid extended attribute key combination";
    case STATUS_SUBDOC_XATTR_UNKNOWN_MACRO:
      return "Unknown extended attribute macro";
    case STATUS_SUBDOC_XATTR_UNKNOWN_VATTR:
      return "Unknown virtual extended attribute";
    case STATUS_SUBDOC_XATTR_CANT_MODIFY_VATTR:
      return "Cannot modify virtual extended attribute";
    case STATUS_SUBDOC_MULTI_PATH_FAILURE_DELETED:
      return "Sub-document multi-path operation failed on deleted document";
    case STATUS_SUBDOC_INVALID_XATTR_ORDER:
      return "Invalid extended attribute order";
    case STATUS_SUBDOC_XATTR_UNKNOWN_VATTR_MACRO:
      return "Unknown virtual extended attribute macro";
    case STATUS_SUBDOC_CAN_ONLY_REVIVE_DELETED_DOCUMENTS:
      return "Can only revive deleted documents";
    case STATUS_SUBDOC_DELETED_DOCUMENT_CANT_HAVE_VALUE:
      return "Deleted document cannot have a value";
    case STATUS_XATTR_EINVAL:
      return "Invalid extended attributes";
  }
  return "Unknown status";
}

// Helper method to format error messages with status codes
std::string CouchbaseOperations::CouchbaseResponse::formatErrorMessage(
    uint16_t status_code, const std::string& operation,
    const std::string& error_msg) {
  if (error_msg.empty()) {
    return butil::string_printf("%s failed with status 0x%02x (%s)",
                                operation.c_str(), status_code,
                                statusStr((Status)status_code));
  } else {
    return butil::string_printf(
        "%s failed with status 0x%02x (%s): %s", operation.c_str(), status_code,
        statusStr((Status)status_code), error_msg.c_str());
  }
}

// MUST NOT have extras.
// MUST have key.
// MUST NOT have value.
bool CouchbaseOperations::CouchbaseRequest::getOrDelete(
    uint8_t command, const butil::StringPiece& key, uint8_t coll_id) {
  // Collection ID
  uint8_t collection_id = coll_id;
  uint16_t VBucket_id = hashCrc32(key.data(), key.size());
  const policy::CouchbaseRequestHeader header = {
      policy::CB_MAGIC_REQUEST, command,
      butil::HostToNet16(key.size() + 1),  // Key
      0,                                   // extras length
      policy::CB_BINARY_RAW_BYTES,         // data type
      butil::HostToNet16(VBucket_id),
      butil::HostToNet32(key.size() +
                         sizeof(collection_id)),  // total body length includes
                                                  // key and collection id
      0, 0};
  if (_buf.append(&header, sizeof(header))) {
    return false;
  }
  if (_buf.append(&collection_id, sizeof(collection_id))) {
    return false;
  }
  if (_buf.append(key.data(), key.size())) {
    return false;
  }
  ++_pipelined_count;
  return true;
}

// collectionID fetching either from the metadata cache or if doesn't exist then
// fetch from the server.
bool CouchbaseOperations::CouchbaseRequest::getCachedOrFetchCollectionId(
    std::string collection_name, uint8_t* coll_id,
    brpc::CouchbaseManifestManager* metadata_tracking, brpc::Channel* channel,
    const std::string& server, const std::string& selected_bucket,
    std:: unordered_map<std::string, CouchbaseManifestManager::CollectionManifest>*
        local_cache) {
  if (collection_name.empty()) {
    DEBUG_PRINT("Empty collection name");
    return false;
  }
  if (channel == nullptr) {
    DEBUG_PRINT("No channel found, make sure to call Authenticate() first");
    return false;
  }
  if (server.empty()) {
    DEBUG_PRINT("Server is empty, make sure to call Authenticate() first");
    return false;
  }
  if (selected_bucket.empty()) {
    DEBUG_PRINT("No bucket selected, make sure to call SelectBucket() first");
    return false;
  }

  brpc::CouchbaseManifestManager::CollectionManifest manifest;
  // check if the server/bucket exists in the cached collection manifest
  if (!metadata_tracking->getBucketToCollectionManifest(server, selected_bucket,
                                                        &manifest)) {
    DEBUG_PRINT("No cached collection manifest found for bucket "
                << selected_bucket << " on server " << server
                << ", fetching from server");
    // No cached manifest found, fetch from server
    if (!metadata_tracking->refreshCollectionManifest(
            channel, server, selected_bucket, local_cache)) {
      return false;
    }
    // local cache will also be updated in refreshCollectionManifest
    // get the reference to collectionID from local cache
    if (!getLocalCachedCollectionId(selected_bucket, "_default",
                                    collection_name, coll_id)) {
      // collectionID not found in the latest manifest fetched from server
      return false;
    }
    // collectionID has been found in the latest manifest fetched from server
    // and is stored in coll_id
    return true;
  } else {
    // check if collection name to id mapping exists.
    if (!metadata_tracking->getManifestToCollectionId(
            &manifest, "_default", collection_name, coll_id)) {
      // Just to verify that the collectionID does not exist in the manifest
      // refresh manifest from server and try again
      if (!metadata_tracking->refreshCollectionManifest(
              channel, server, selected_bucket, local_cache)) {
        return false;
      }
      // local cache will also be updated in refreshCollectionManifest
      // get the reference to collectionID from local cache
      if (!getLocalCachedCollectionId(selected_bucket, "_default",
                                      collection_name, coll_id)) {
        // collectionID not found in the latest manifest fetched from server
        return false;
      }
      // collectionID has been found in the latest manifest fetched from server
      // and is stored in coll_id
      return true;
    }
    // update the local cache with the manifest in global cache
    (*local_collection_manifest_cache)[selected_bucket] = manifest;
    // collectionID found in the cached manifest
    return true;
  }
}

bool CouchbaseOperations::CouchbaseRequest::getRequest(
    const butil::StringPiece& key, std::string collection_name,
    brpc::Channel* channel, const std::string& server, const std::string& bucket) {
  DEBUG_PRINT("getRequest called with key: "
              << key << ", collection_name: " << collection_name
              << ", server: " << server << ", bucket: " << bucket);
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      DEBUG_PRINT("Local collection manifest cache is empty in getRequest");
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "getRequest");
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      DEBUG_PRINT("Collection id not found in local cache in getRequest");
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "getRequest");
        return false;
      }
    }
  }
  DEBUG_PRINT("getRequest using coll_id: " << (int)coll_id);
  return getOrDelete(policy::CB_BINARY_GET, key, coll_id);
}

bool CouchbaseOperations::CouchbaseRequest::deleteRequest(
    const butil::StringPiece& key, std::string collection_name,
    brpc::Channel* channel, const std::string& server, const std::string& bucket) {
  DEBUG_PRINT("deleteRequest called with key: "
              << key << ", collection_name: " << collection_name
              << ", server: " << server << ", bucket: " << bucket);
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      DEBUG_PRINT("Local collection manifest cache is empty in deleteRequest");
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "deleteRequest");
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      DEBUG_PRINT("Collection id not found in local cache in deleteRequest");
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "deleteRequest");
        return false;
      }
    }
  }
  DEBUG_PRINT("deleteRequest using coll_id: " << (int)coll_id);
  return getOrDelete(policy::CB_BINARY_DELETE, key, coll_id);
}

struct FlushHeaderWithExtras {
  policy::CouchbaseRequestHeader header;
  uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(FlushHeaderWithExtras) == 28, must_match);

bool CouchbaseOperations::CouchbaseResponse::popGet(butil::IOBuf* value,
                                                    uint32_t* flags,
                                                    uint64_t* cas_value) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));
  if (header.command != (uint8_t)policy::CB_BINARY_GET) {
    butil::string_printf(&_err, "not a GET response");
    return false;
  }
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                         (unsigned)n, (unsigned)sizeof(header),
                         header.total_body_length);
    return false;
  }
  if (header.status != (uint16_t)STATUS_SUCCESS) {
    if (DBUG && header.extras_length != 0) {
      DEBUG_PRINT("GET response must not have flags");
    }
    if (DBUG && header.key_length != 0) {
      DEBUG_PRINT("GET response must not have key");
    }
    const int value_size = (int)header.total_body_length -
                           (int)header.extras_length - (int)header.key_length;
    _status_code = header.status;
    if (value_size < 0) {
      butil::string_printf(&_err, "value_size=%d is non-negative", value_size);
      return false;
    }
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    if (value_size > 0) {
      std::string error_msg;
      _buf.cutn(&error_msg, value_size);
      _err = formatErrorMessage(header.status, "GET operation", error_msg);
    } else {
      _err = formatErrorMessage(header.status, "GET operation");
    }
    return false;
  }
  if (header.extras_length != 4u) {
    butil::string_printf(
        &_err, "GET response must have flags as extras, actual length=%u",
        header.extras_length);
    return false;
  }
  if (header.key_length != 0) {
    butil::string_printf(&_err, "GET response must not have key");
    return false;
  }
  const int value_size = (int)header.total_body_length -
                         (int)header.extras_length - (int)header.key_length;
  if (value_size < 0) {
    butil::string_printf(&_err, "value_size=%d is non-negative", value_size);
    return false;
  }
  _buf.pop_front(sizeof(header));
  uint32_t raw_flags = 0;
  _buf.cutn(&raw_flags, sizeof(raw_flags));
  if (flags) {
    *flags = butil::NetToHost32(raw_flags);
  }
  if (value) {
    value->clear();
    _buf.cutn(value, value_size);
  }
  if (cas_value) {
    *cas_value = header.cas_value;
  }
  _err.clear();
  return true;
}

bool CouchbaseOperations::CouchbaseResponse::popGet(std::string* value,
                                                    uint32_t* flags,
                                                    uint64_t* cas_value) {
  butil::IOBuf tmp;
  if (popGet(&tmp, flags, cas_value)) {
    tmp.copy_to(value);
    return true;
  }
  return false;
}

// MUST NOT have extras
// MUST NOT have key
// MUST NOT have value
bool CouchbaseOperations::CouchbaseResponse::popDelete() {
  return popStore(policy::CB_BINARY_DELETE, NULL);
}

struct StoreHeaderWithExtras {
  policy::CouchbaseRequestHeader header;
  uint32_t flags;
  uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(StoreHeaderWithExtras) == 32, must_match);
const size_t STORE_EXTRAS =
    sizeof(StoreHeaderWithExtras) - sizeof(policy::CouchbaseRequestHeader);
// MUST have extras.
// MUST have key.
// MAY have value.
// Extra data for set/add/replace:
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| Flags                                                         |
//   +---------------+---------------+---------------+---------------+
//  4| Expiration                                                    |
//   +---------------+---------------+---------------+---------------+
//   Total 8 bytes
bool CouchbaseOperations::CouchbaseRequest::store(
    uint8_t command, const butil::StringPiece& key,
    const butil::StringPiece& value, uint32_t flags, uint32_t exptime,
    uint64_t cas_value, uint8_t coll_id) {
  // add collection id
  //  uint16_t collection_id = 0x00;
  uint8_t collection_id = coll_id;
  uint16_t vBucket_id = hashCrc32(key.data(), key.size());
  StoreHeaderWithExtras header_with_extras = {
      {policy::CB_MAGIC_REQUEST, command,
       butil::HostToNet16(key.size() +
                          1),  // collection id is not included in part of key,
                               // so not including it in key length.
       STORE_EXTRAS, policy::CB_JSON, butil::HostToNet16(vBucket_id),
       butil::HostToNet32(STORE_EXTRAS + sizeof(collection_id) + key.size() +
                          value.size()),  // total body length
       0, butil::HostToNet64(cas_value)},
      butil::HostToNet32(flags),
      butil::HostToNet32(exptime)};
  if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
    return false;
  }
  if (_buf.append(&collection_id, sizeof(collection_id))) {
    return false;
  }
  if (_buf.append(key.data(), key.size())) {
    return false;
  }
  if (_buf.append(value.data(), value.size())) {
    return false;
  }
  ++_pipelined_count;
  return true;
}

// MUST have CAS
// MUST NOT have extras
// MUST NOT have key
// MUST NOT have value
bool CouchbaseOperations::CouchbaseResponse::popStore(uint8_t command,
                                                      uint64_t* cas_value) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));
  if (header.command != command) {
    butil::string_printf(&_err, "Not a STORE response");
    return false;
  }
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "Not enough data");
    return false;
  }
  if (DBUG && header.extras_length != 0) {
    DEBUG_PRINT("STORE response must not have flags");
  }
  if (DBUG && header.key_length != 0) {
    DEBUG_PRINT("STORE response must not have key");
  }
  int value_size = (int)header.total_body_length - (int)header.extras_length -
                   (int)header.key_length;
  if (header.status != (uint16_t)STATUS_SUCCESS) {
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    _status_code = header.status;
    if (value_size > 0) {
      std::string error_msg;
      _buf.cutn(&error_msg, value_size);
      _err = formatErrorMessage(
          header.status, couchbaseBinaryCommandToString(command), error_msg);
    } else {
      _err = formatErrorMessage(header.status,
                                couchbaseBinaryCommandToString(command));
    }
    return false;
  }
  if (DBUG && value_size != 0) {
    DEBUG_PRINT("STORE response must not have value, actually=" << value_size);
  }
  _buf.pop_front(sizeof(header) + header.total_body_length);
  if (cas_value) {
    *cas_value = header.cas_value;
  }
  _err.clear();
  return true;
}

const char*
CouchbaseOperations::CouchbaseResponse::couchbaseBinaryCommandToString(
    uint8_t cmd) {
  switch (cmd) {
    case 0x1f:
      return "CB_HELLO_SELECT_FEATURES";
    case 0x89:
      return "CB_SELECT_BUCKET";
    case 0xBC:
      return "CB_GET_SCOPE_ID";
    case 0x00:
      return "CB_BINARY_GET";
    case 0x01:
      return "CB_BINARY_SET";
    case 0x02:
      return "CB_BINARY_ADD";
    case 0x03:
      return "CB_BINARY_REPLACE";
    case 0x04:
      return "CB_BINARY_DELETE";
    case 0x05:
      return "CB_BINARY_INCREMENT";
    case 0x06:
      return "CB_BINARY_DECREMENT";
    case 0x07:
      return "CB_BINARY_QUIT";
    case 0x08:
      return "CB_BINARY_FLUSH";
    case 0x09:
      return "CB_BINARY_GETQ";
    case 0x0a:
      return "CB_BINARY_NOOP";
    case 0x0b:
      return "CB_BINARY_VERSION";
    case 0x0c:
      return "CB_BINARY_GETK";
    case 0x0d:
      return "CB_BINARY_GETKQ";
    case 0x0e:
      return "CB_BINARY_APPEND";
    case 0x0f:
      return "CB_BINARY_PREPEND";
    case 0x10:
      return "CB_BINARY_STAT";
    case 0x11:
      return "CB_BINARY_SETQ";
    case 0x12:
      return "CB_BINARY_ADDQ";
    case 0x13:
      return "CB_BINARY_REPLACEQ";
    case 0x14:
      return "CB_BINARY_DELETEQ";
    case 0x15:
      return "CB_BINARY_INCREMENTQ";
    case 0x16:
      return "CB_BINARY_DECREMENTQ";
    case 0x17:
      return "CB_BINARY_QUITQ";
    case 0x18:
      return "CB_BINARY_FLUSHQ";
    case 0x19:
      return "CB_BINARY_APPENDQ";
    case 0x1a:
      return "CB_BINARY_PREPENDQ";
    case 0x1c:
      return "CB_BINARY_TOUCH";
    case 0x1d:
      return "CB_BINARY_GAT";
    case 0x1e:
      return "CB_BINARY_GATQ";
    case 0x23:
      return "CB_BINARY_GATK";
    case 0x24:
      return "CB_BINARY_GATKQ";
    case 0x20:
      return "CB_BINARY_SASL_LIST_MECHS";
    case 0x21:
      return "CB_BINARY_SASL_AUTH";
    case 0x22:
      return "CB_BINARY_SASL_STEP";
    case 0xb5:
      return "CB_GET_CLUSTER_CONFIG";
    case 0xba:
      return "CB_GET_COLLECTIONS_MANIFEST";
    case 0xbb:
      return "CB_COLLECTIONS_GET_CID";
    default:
      return "UNKNOWN_COMMAND";
  }
}

bool CouchbaseOperations::CouchbaseRequest::upsertRequest(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value,
    std::string collection_name, brpc::Channel* channel, const std::string& server,
    const std::string& bucket) {
  DEBUG_PRINT("upsertRequest called with key: "
              << key << ", value: " << value
              << ", collection_name: " << collection_name
              << ", server: " << server << ", bucket: " << bucket);
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      DEBUG_PRINT("Local collection manifest cache is empty in upsertRequest");
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "upsertRequest");
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      DEBUG_PRINT("Collection id not found in local cache in upsertRequest");
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "upsertRequest");
        return false;
      }
    }
  }
  DEBUG_PRINT("upsertRequest using coll_id: " << (int)coll_id);
  return store(policy::CB_BINARY_SET, key, value, flags, exptime, cas_value,
               coll_id);
}

bool CouchbaseOperations::CouchbaseRequest::getCollectionManifest() {
  const policy::CouchbaseRequestHeader header = {
      policy::CB_MAGIC_REQUEST,
      policy::CB_GET_COLLECTIONS_MANIFEST,
      0,  // no key
      0,  // no extras
      policy::CB_BINARY_RAW_BYTES,
      0,  // no vbucket
      0,  // no body (no key, no extras, no value)
      0,  // opaque
      0   // no CAS
  };
  if (_buf.append(&header, sizeof(header))) {
    return false;
  }
  ++_pipelined_count;
  return true;
}

bool CouchbaseOperations::CouchbaseRequest::addRequest(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value,
    std::string collection_name, brpc::Channel* channel, const std::string& server,
    const std::string& bucket) {
  DEBUG_PRINT("addRequest called with key: "
              << key << ", value: " << value
              << ", collection_name: " << collection_name
              << ", server: " << server << ", bucket: " << bucket);
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      DEBUG_PRINT("Local collection manifest cache is empty in addRequest");
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "addRequest");
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      DEBUG_PRINT("Collection id not found in local cache in addRequest");
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        DEBUG_PRINT(
            "Failed to get collection id from global cache or server in "
            "addRequest");
        return false;
      }
    }
  }
  DEBUG_PRINT("addRequest using coll_id: " << (int)coll_id);
  return store(policy::CB_BINARY_ADD, key, value, flags, exptime, cas_value,
               coll_id);
}

bool CouchbaseOperations::CouchbaseRequest::appendRequest(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value,
    std::string collection_name, brpc::Channel* channel, const std::string& server,
    const std::string& bucket) {
  if (value.empty()) {
    DEBUG_PRINT("value to append must be non-empty");
    return false;
  }
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        return false;
      }
    }
  }
  return store(policy::CB_BINARY_APPEND, key, value, flags, exptime, cas_value,
               coll_id);
}

bool CouchbaseOperations::CouchbaseRequest::prependRequest(
    const butil::StringPiece& key, const butil::StringPiece& value,
    uint32_t flags, uint32_t exptime, uint64_t cas_value,
    std::string collection_name, brpc::Channel* channel, const std::string& server,
    const std::string& bucket) {
  if (value.empty()) {
    DEBUG_PRINT("value to prepend must be non-empty");
    return false;
  }
  uint8_t coll_id = 0;  // default collection ID
  if (collection_name != "_default") {
    // check if the local cache is empty or not.
    if (local_collection_manifest_cache->empty()) {
      // if local cache is empty, goto global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        return false;
      }
    }
    // check if the collection id is available in the local cache
    else if (!getLocalCachedCollectionId(bucket, "_default", collection_name,
                                         &coll_id)) {
      // if not check in the global cache or fetch from server
      if (!getCachedOrFetchCollectionId(
              collection_name, &coll_id, metadata_tracking, channel, server,
              bucket, local_collection_manifest_cache)) {
        return false;
      }
    }
  }
  return store(policy::CB_BINARY_PREPEND, key, value, flags, exptime, cas_value,
               coll_id);
}

bool CouchbaseOperations::CouchbaseResponse::popAuthenticate(
    uint64_t* cas_value) {
  return popStore(policy::CB_BINARY_SASL_AUTH, cas_value);
}
bool CouchbaseOperations::CouchbaseResponse::popHello(uint64_t* cas_value) {
  return popStore(policy::CB_HELLO_SELECT_FEATURES, cas_value);
}
bool CouchbaseOperations::CouchbaseResponse::popUpsert(uint64_t* cas_value) {
  return popStore(policy::CB_BINARY_SET, cas_value);
}
bool CouchbaseOperations::CouchbaseResponse::popAdd(uint64_t* cas_value) {
  return popStore(policy::CB_BINARY_ADD, cas_value);
}
// Warning: Not tested
// bool CouchbaseOperations::CouchbaseResponse::PopReplace(uint64_t* cas_value)
// {
//   return popStore(policy::CB_BINARY_REPLACE, cas_value);
// }
bool CouchbaseOperations::CouchbaseResponse::popAppend(uint64_t* cas_value) {
  return popStore(policy::CB_BINARY_APPEND, cas_value);
}
bool CouchbaseOperations::CouchbaseResponse::popPrepend(uint64_t* cas_value) {
  return popStore(policy::CB_BINARY_PREPEND, cas_value);
}
bool CouchbaseOperations::CouchbaseResponse::popSelectBucket(
    uint64_t* cas_value) {
  if (popStore(policy::CB_SELECT_BUCKET, cas_value) == false) {
    DEBUG_PRINT("Failed to select bucket: " << _err);
    return false;
  }
  // Note: Bucket tracking is now handled at CouchbaseOperations level, not
  // per-thread
  return true;
}
// Collection-related response method
bool CouchbaseOperations::CouchbaseResponse::popCollectionId(
    uint8_t* collection_id) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));

  if (header.command != policy::CB_COLLECTIONS_GET_CID) {
    butil::string_printf(&_err, "Not a collection ID response");
    return false;
  }

  // Making sure buffer has the whole body (extras + key + value)
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "Not enough data");
    return false;
  }

  if (header.status != 0) {
    // handle error case
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    // Possibly read error message from value if present
    size_t value_size =
        header.total_body_length - header.extras_length - header.key_length;
    if (value_size > 0) {
      std::string err_msg;
      _buf.cutn(&err_msg, value_size);
      _err =
          formatErrorMessage(header.status, "Collection ID request", err_msg);
    } else {
      _err = formatErrorMessage(header.status, "Collection ID request");
    }
    return false;
  }

  // Success case: we expect extras_length >= 12 (8 bytes manifest + 4 bytes
  // collection id)
  if (header.extras_length < 12) {
    butil::string_printf(&_err, "Extras too small to contain collection ID");
    // remove the response from buffer so you don't re‐process
    _buf.pop_front(sizeof(header) + header.total_body_length);
    return false;
  }

  // Skip header
  _buf.pop_front(sizeof(header));

  // return true;
  uint64_t manifest_id_net = 0;
  _buf.copy_to(reinterpret_cast<uint64_t*>(&manifest_id_net),
               sizeof(manifest_id_net));
  // You may convert this if needed:
  uint64_t manifest_id = butil::NetToHost64(manifest_id_net);
  DEBUG_PRINT("Manifest ID: " << manifest_id);
  _buf.pop_front(sizeof(manifest_id_net));

  // Next 1 bytes → collection ID (u8)
  uint32_t cid_net = 0;
  _buf.copy_to(reinterpret_cast<uint8_t*>(&cid_net), sizeof(cid_net));
  uint8_t cid_host = butil::NetToHost32(cid_net);
  *collection_id = static_cast<int>(cid_host);
  _buf.pop_front(sizeof(cid_net));

  _buf.pop_front(header.total_body_length);
  _err.clear();
  return true;
}

bool CouchbaseOperations::CouchbaseResponse::popManifest(
    std::string* manifest_json) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));

  if (header.command != policy::CB_GET_COLLECTIONS_MANIFEST) {
    butil::string_printf(&_err, "Not a get collections manifest response");
    return false;
  }

  // Making sure buffer has the whole body (extras + key + value)
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "Not enough data");
    return false;
  }

  if (header.status != 0) {
    // handle error case
    if (header.extras_length != 0) {
      DEBUG_PRINT("Get Collections Manifest response must not have extras");
    }
    if (header.key_length != 0) {
      DEBUG_PRINT("Get Collections Manifest response must not have key");
    }
    _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
    // Possibly read error message from value if present
    size_t value_size =
        header.total_body_length - header.extras_length - header.key_length;
    if (value_size > 0) {
      std::string err_msg;
      _buf.cutn(&err_msg, value_size);
      _err = formatErrorMessage(header.status, "Get Collections Manifest",
                                err_msg);
    } else {
      _err = formatErrorMessage(header.status, "Get Collections Manifest");
    }
    return false;
  }

  // Success case: the manifest should be in the value section
  size_t value_size =
      header.total_body_length - header.extras_length - header.key_length;
  if (value_size == 0) {
    butil::string_printf(&_err, "No manifest data in response");
    _buf.pop_front(sizeof(header) + header.total_body_length);
    return false;
  }

  // Skip header and any extras/key
  _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);

  // Read the manifest JSON from the value section
  _buf.cutn(manifest_json, value_size);

  _err.clear();
  return true;
}

struct IncrHeaderWithExtras {
  policy::CouchbaseRequestHeader header;
  uint64_t delta;
  uint64_t initial_value;
  uint32_t exptime;
} __attribute__((packed));
BAIDU_CASSERT(sizeof(IncrHeaderWithExtras) == 44, must_match);

const size_t INCR_EXTRAS =
    sizeof(IncrHeaderWithExtras) - sizeof(policy::CouchbaseRequestHeader);

// MUST have extras.
// MUST have key.
// MUST NOT have value.
// Extra data for incr/decr:
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| Delta to add / subtract                                      |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
//  8| Initial value                                                 |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
// 16| Expiration                                                    |
//   +---------------+---------------+---------------+---------------+
//   Total 20 bytes
bool CouchbaseOperations::CouchbaseRequest::counter(
    uint8_t command, const butil::StringPiece& key, uint64_t delta,
    uint64_t initial_value, uint32_t exptime) {
  IncrHeaderWithExtras header_with_extras = {
      {policy::CB_MAGIC_REQUEST, command, butil::HostToNet16(key.size()),
       INCR_EXTRAS, policy::CB_BINARY_RAW_BYTES, 0,
       butil::HostToNet32(INCR_EXTRAS + key.size()), 0, 0},
      butil::HostToNet64(delta),
      butil::HostToNet64(initial_value),
      butil::HostToNet32(exptime)};
  if (_buf.append(&header_with_extras, sizeof(header_with_extras))) {
    return false;
  }
  if (_buf.append(key.data(), key.size())) {
    return false;
  }
  ++_pipelined_count;
  return true;
}

// MUST NOT have extras.
// MUST NOT have key.
// MUST have value.
// Byte/     0       |       1       |       2       |       3       |
//    /              |               |               |               |
//   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
//   +---------------+---------------+---------------+---------------+
//  0| 64-bit unsigned response.                                     |
//   |                                                               |
//   +---------------+---------------+---------------+---------------+
//   Total 8 bytes
bool CouchbaseOperations::CouchbaseResponse::popCounter(uint8_t command,
                                                        uint64_t* new_value,
                                                        uint64_t* cas_value) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));
  if (header.command != command) {
    butil::string_printf(&_err, "not a INCR/DECR response");
    return false;
  }
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                         (unsigned)n, (unsigned)sizeof(header),
                         header.total_body_length);
    return false;
  }
  if (DBUG && header.extras_length != 0) {
    DEBUG_PRINT("INCR/DECR response must not have flags");
  }
  if (DBUG && header.key_length != 0) {
    DEBUG_PRINT("INCR/DECR response must not have key");
  }
  const int value_size = (int)header.total_body_length -
                         (int)header.extras_length - (int)header.key_length;
  _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);

  if (header.status != (uint16_t)STATUS_SUCCESS) {
    if (value_size < 0) {
      butil::string_printf(&_err, "value_size=%d is negative", value_size);
    } else {
      if (value_size > 0) {
        std::string error_msg;
        _buf.cutn(&error_msg, value_size);
        _err =
            formatErrorMessage(header.status, "Counter operation", error_msg);
      } else {
        _err = formatErrorMessage(header.status, "Counter operation");
      }
    }
    return false;
  }
  if (value_size != 8) {
    butil::string_printf(&_err, "value_size=%d is not 8", value_size);
    return false;
  }
  uint64_t raw_value = 0;
  _buf.cutn(&raw_value, sizeof(raw_value));
  *new_value = butil::NetToHost64(raw_value);
  if (cas_value) {
    *cas_value = header.cas_value;
  }
  _err.clear();
  return true;
}

// MUST NOT have extras.
// MUST NOT have key.
// MUST NOT have value.
bool CouchbaseOperations::CouchbaseRequest::versionRequest() {
  const policy::CouchbaseRequestHeader header = {policy::CB_MAGIC_REQUEST,
                                                 policy::CB_BINARY_VERSION,
                                                 0,
                                                 0,
                                                 policy::CB_BINARY_RAW_BYTES,
                                                 0,
                                                 0,
                                                 0,
                                                 0};
  if (_buf.append(&header, sizeof(header))) {
    return false;
  }
  ++_pipelined_count;
  return true;
}

bool CouchbaseOperations::CouchbaseResponse::popVersion(std::string* version) {
  const size_t n = _buf.size();
  policy::CouchbaseResponseHeader header;
  if (n < sizeof(header)) {
    butil::string_printf(&_err, "buffer is too small to contain a header");
    return false;
  }
  _buf.copy_to(&header, sizeof(header));
  if (header.command != policy::CB_BINARY_VERSION) {
    butil::string_printf(&_err, "not a VERSION response");
    return false;
  }
  if (n < sizeof(header) + header.total_body_length) {
    butil::string_printf(&_err, "response=%u < header=%u + body=%u",
                         (unsigned)n, (unsigned)sizeof(header),
                         header.total_body_length);
    return false;
  }
  if (DBUG && header.extras_length != 0) {
    DEBUG_PRINT("VERSION response must not have flags");
  }
  if (DBUG && header.key_length != 0) {
    DEBUG_PRINT("VERSION response must not have key");
  }
  const int value_size = (int)header.total_body_length -
                         (int)header.extras_length - (int)header.key_length;
  _buf.pop_front(sizeof(header) + header.extras_length + header.key_length);
  if (value_size < 0) {
    butil::string_printf(&_err, "value_size=%d is negative", value_size);
    return false;
  }
  if (header.status != (uint16_t)STATUS_SUCCESS) {
    if (value_size > 0) {
      std::string error_msg;
      _buf.cutn(&error_msg, value_size);
      _err = formatErrorMessage(header.status, "Version request", error_msg);
    } else {
      _err = formatErrorMessage(header.status, "Version request");
    }
    return false;
  }
  if (version) {
    version->clear();
    _buf.cutn(version, value_size);
  }
  _err.clear();
  return true;
}

bool sendRequest(CouchbaseOperations::operation_type op_type, const std::string& key,
                 const std::string& value, std::string collection_name,
                 CouchbaseOperations::Result* result, brpc::Channel* channel,
                 const std::string& server, const std::string& bucket,
                 CouchbaseOperations::CouchbaseRequest* request,
                 CouchbaseOperations::CouchbaseResponse* response) {
  if (channel == nullptr) {
    DEBUG_PRINT("No channel found, make sure to call Authenticate() first");
    result->error_message =
        "No channel found, make sure to call Authenticate() first";
    return false;
  }
  if (server.empty()) {
    DEBUG_PRINT("Server is empty, make sure to call Authenticate() first");
    result->error_message =
        "Server is empty, make sure to call Authenticate() first";
    return false;
  }
  if (bucket.empty()) {
    DEBUG_PRINT("No bucket selected, make sure to call SelectBucket() first");
    result->error_message =
        "No bucket selected, make sure to call SelectBucket() first";
    return false;
  }
  brpc::Controller cntl;
  bool request_created = false;
  switch (op_type) {
    case CouchbaseOperations::GET:
      request_created =
          request->getRequest(key, collection_name, channel, server, bucket);
      break;
    case CouchbaseOperations::UPSERT:
      request_created = request->upsertRequest(
          key, value, 0, 0, 0, collection_name, channel, server, bucket);
      break;
    case CouchbaseOperations::ADD:
      request_created = request->addRequest(
          key, value, 0, 0, 0, collection_name, channel, server, bucket);
      break;
    case CouchbaseOperations::APPEND:
      request_created = request->appendRequest(
          key, value, 0, 0, 0, collection_name, channel, server, bucket);
      break;
    case CouchbaseOperations::PREPEND:
      request_created = request->prependRequest(
          key, value, 0, 0, 0, collection_name, channel, server, bucket);
      break;
    case CouchbaseOperations::DELETE:
      request_created =
          request->deleteRequest(key, collection_name, channel, server, bucket);
      break;
    default:
      DEBUG_PRINT("Unsupported operation type");
      result->success = false;
      result->value = "";
      result->error_message = "Unsupported operation type";
      return false;
  }
  if (!request_created) {
    DEBUG_PRINT("CollectionID does not exist." << op_type);
    result->success = false;
    result->value = "";
    result->error_message =
        "CollectionID does not exist." + std::to_string(op_type);
    result->status_code = 0x88;  // using 0x88 as the only possible failure code
                                 // that indicates the collectionID is not found
    return false;
  }
  channel->CallMethod(NULL, &cntl, request, response, NULL);
  if (cntl.Failed()) {
    DEBUG_PRINT("Failed to perform operation on key: "
                << key << " to Couchbase: " << cntl.ErrorText());
    result->success = false;
    result->value = "";
    result->error_message = cntl.ErrorText();
    return false;
  }
  if (op_type == CouchbaseOperations::GET) {
    std::string value;
    uint32_t flags = 0;
    uint64_t cas = 0;
    if (response->popGet(&value, &flags, &cas) == false) {
      result->success = false;
      result->value = "";
      result->error_message = response->lastError();
      result->status_code = response->_status_code;
      if (result->status_code == 0x88) {
        DEBUG_PRINT(
            "CollectionID does not exist on server, need to refresh collection "
            "manifest from server");
        // could have called sendRequest recursively,
        // but if somehow the collectionID keeps on chaning, it would lead to
        // infinite recursion and stack overflow in the end. so we retry once
        // here instead and return failure if it still fails.

        // (0x88) unknown collection, this means that the collection_manifest
        // has been updated on the server side. The collectionID present in the
        // local cache/global cache is no longer valid. This can happen if a
        // collection is deleted and recreated with the same name.
        if (!request->metadata_tracking->refreshCollectionManifest(
                channel, server, bucket,
                request->local_collection_manifest_cache)) {
          DEBUG_PRINT("Failed to refresh collection manifest");
          result->error_message = "Failed to refresh collection manifest";
        } else {
          DEBUG_PRINT("Successfully refreshed collection manifest");
          // retry the request;
          request->Clear();
          response->Clear();
          cntl.Reset();
          if (!request->getRequest(key, collection_name, channel, server,
                                   bucket)) {
            DEBUG_PRINT("CollectionID does not exist.");
            result->success = false;
            result->value = "";
            result->error_message = "CollectionID does not exist.";
            result->status_code =
                0x88;  // using 0x88 as the only possible failure code that
                       // indicates the collectionID is not found
            return false;
          }
          channel->CallMethod(NULL, &cntl, request, response, NULL);
          if (cntl.Failed()) {
            DEBUG_PRINT("Failed to perform operation on key: "
                        << key << " to Couchbase: " << cntl.ErrorText());
            result->success = false;
            result->value = "";
            result->error_message = cntl.ErrorText();
            return false;  // return on failure
          }
          if (response->popGet(&value, &flags, &cas) == false) {
            result->success = false;
            result->value = "";
            result->error_message = response->lastError();
            result->status_code = response->_status_code;
            return false;  // return on failure
          }
          // Successfully got the value after retry
          result->success = true;
          result->value = value;
          result->status_code = 0;
          return true;
        }
      }
      return false;
    }
    // Successfully got the value
    result->success = true;
    result->value = value;
    result->status_code = 0;
    return true;
  } else {
    uint64_t cas_value = 0;
    // pop response on the basis of operation type
    bool pop_success = false;
    switch (op_type) {
      case CouchbaseOperations::UPSERT:
        pop_success = response->popUpsert(&cas_value);
        break;
      case CouchbaseOperations::ADD:
        pop_success = response->popAdd(&cas_value);
        break;
      case CouchbaseOperations::APPEND:
        pop_success = response->popAppend(&cas_value);
        break;
      case CouchbaseOperations::PREPEND:
        pop_success = response->popPrepend(&cas_value);
        break;
      case CouchbaseOperations::DELETE:
        pop_success = response->popDelete();
        break;
      default:
        DEBUG_PRINT("Unsupported operation type in response pop");
        result->success = false;
        result->value = "";
        result->error_message = "Unsupported operation type in response pop";
        return false;
    }
    if (!pop_success) {
      result->success = false;
      result->value = "";
      result->error_message = response->lastError();
      result->status_code = response->_status_code;
      if (result->status_code == 0x88) {
        // (0x88) unknown collection, this typically means that the
        // collection_manifest has been updated on the server side. and the
        // client have a stale copy of collection manifest. In this case, we
        // need to refresh the collection manifest and retry the operation.
        if (!request->metadata_tracking->refreshCollectionManifest(
                channel, server, bucket,
                request->local_collection_manifest_cache)) {
          DEBUG_PRINT("Failed to refresh collection manifest");
          result->error_message = "Failed to refresh collection manifest";
          return false;
        }
        // could have called sendRequest recursively,
        // but if somehow the collectionID keeps on chaning, it would lead to
        // infinite recursion and stack overflow in the end. so we retry once
        // here instead and return failure if it still fails.
        DEBUG_PRINT("Successfully refreshed collection manifest");
        // retry the request;
        request->Clear();
        response->Clear();
        switch (op_type) {
          case CouchbaseOperations::UPSERT:
            request->upsertRequest(key, value, 0, 0, 0, collection_name,
                                   channel, server, bucket);
            break;
          case CouchbaseOperations::ADD:
            request->addRequest(key, value, 0, 0, 0, collection_name, channel,
                                server, bucket);
            break;
          case CouchbaseOperations::APPEND:
            request->appendRequest(key, value, 0, 0, 0, collection_name,
                                   channel, server, bucket);
            break;
          case CouchbaseOperations::PREPEND:
            request->prependRequest(key, value, 0, 0, 0, collection_name,
                                    channel, server, bucket);
            break;
          case CouchbaseOperations::DELETE:
            request->deleteRequest(key, collection_name, channel, server,
                                   bucket);
            break;
          default:
            DEBUG_PRINT("Unsupported operation type in response pop");
            result->success = false;
            result->value = "";
            result->error_message =
                "Unsupported operation type in response pop";
            return false;
        }
        channel->CallMethod(NULL, &cntl, request, response, NULL);
        if (cntl.Failed()) {
          DEBUG_PRINT("Failed to perform operation on key: "
                      << key << " to Couchbase: " << cntl.ErrorText());
          result->success = false;
          result->value = "";
          result->error_message = cntl.ErrorText();
          return false;  // return on failure
        }
        pop_success = false;
        switch (op_type) {
          case CouchbaseOperations::UPSERT:
            pop_success = response->popUpsert(&cas_value);
            break;
          case CouchbaseOperations::ADD:
            pop_success = response->popAdd(&cas_value);
            break;
          case CouchbaseOperations::APPEND:
            pop_success = response->popAppend(&cas_value);
            break;
          case CouchbaseOperations::PREPEND:
            pop_success = response->popPrepend(&cas_value);
            break;
          case CouchbaseOperations::DELETE:
            pop_success = response->popDelete();
            break;
          default:
            DEBUG_PRINT("Unsupported operation type in response pop");
            result->success = false;
            result->value = "";
            result->error_message =
                "Unsupported operation type in response pop";
            return false;
        }
        if (!pop_success) {
          result->success = false;
          result->value = "";
          result->error_message = response->lastError();
          result->status_code = response->_status_code;
          return false;  // return on failure
        }
        // Successfully performed the operation after retry
        result->success = true;
        result->value = "";
        result->status_code = 0;
        return true;
      }
      return false;
    }
    // Successfully performed the operation
    // Note: For operations other than GET, we don't have a value to return
    // so we return empty std::string for value.
    result->success = true;
    result->value = "";
    result->status_code = 0;
    return true;
  }
}
CouchbaseOperations::Result CouchbaseOperations::get(const std::string& key,
                                                     std::string collection_name) {
  // create CouchbaseRequest and CouchbaseResponse objects and then using the
  // channel which is created for this thread in authenticate() use it to call()
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  sendRequest(CouchbaseOperations::GET, key, "", collection_name, &result,
              channel_, server_address_, selected_bucket_, &request, &response);
  return result;
}

bool CouchbaseOperations::CouchbaseRequest::getLocalCachedCollectionId(
    const std::string& bucket, const std::string& scope, const std::string& collection,
    uint8_t* collection_id) {
  if (bucket.empty() || scope.empty() || collection.empty()) {
    DEBUG_PRINT("Bucket, scope, and collection names must be non-empty");
    return false;
  }
  auto it = local_collection_manifest_cache->find(bucket);
  if (it != local_collection_manifest_cache->end()) {
    CouchbaseManifestManager::CollectionManifest& manifest = it->second;
    if (manifest.scope_to_collection_id_map.find(scope) !=
        manifest.scope_to_collection_id_map.end()) {
      auto& collection_map = manifest.scope_to_collection_id_map[scope];
      if (collection_map.find(collection) != collection_map.end()) {
        *collection_id = collection_map[collection];
        return true;
      } else {
        DEBUG_PRINT("Collection name not found in local cache: " << collection);
        return false;
      }
    } else {
      DEBUG_PRINT("Scope name not found in local cache: " << scope);
      return false;
    }
  } else {
    DEBUG_PRINT("Bucket name not found in local cache: " << bucket);
    return false;
  }
}

CouchbaseOperations::Result CouchbaseOperations::upsert(
    const std::string& key, const std::string& value, std::string collection_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  sendRequest(CouchbaseOperations::UPSERT, key, value, collection_name, &result,
              channel_, server_address_, selected_bucket_, &request, &response);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::delete_(
    const std::string& key, std::string collection_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  if (!sendRequest(CouchbaseOperations::DELETE, key, "", collection_name,
                   &result, channel_, server_address_, selected_bucket_,
                   &request, &response)) {
    return result;
  }
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::add(const std::string& key,
                                                     const std::string& value,
                                                     std::string collection_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  sendRequest(CouchbaseOperations::ADD, key, value, collection_name, &result,
              channel_, server_address_, selected_bucket_, &request, &response);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::authenticate(
    const std::string& username, const std::string& password,
    const std::string& server_address, const std::string& bucket_name) {
  return authenticateAll(username, password, server_address, bucket_name, false,
                         "");
}

CouchbaseOperations::Result CouchbaseOperations::authenticateSSL(
    const std::string& username, const std::string& password,
    const std::string& server_address, const std::string& bucket_name,
    std::string path_to_cert) {
  return authenticateAll(username, password, server_address, bucket_name, true,
                         path_to_cert);
}

CouchbaseOperations::Result CouchbaseOperations::authenticateAll(
    const std::string& username, const std::string& password,
    const std::string& server_address, const std::string& bucket_name, bool enable_ssl,
    std::string path_to_cert) {
  // Create a channel to the Couchbase server
  brpc::ChannelOptions options;
  options.protocol = brpc::PROTOCOL_COUCHBASE;
  options.connection_type = "single";
  options.timeout_ms = 1000;  // 1 second
  options.max_retry = 3;

  // CRITICAL: Set unique connection_group to prevent connection sharing
  // Each CouchbaseOperations instance connected to same bucket gets its own
  // connection group
  options.connection_group = server_address + bucket_name;

  // enable_ssl
  if (enable_ssl) {
    brpc::ChannelSSLOptions* ssl_options = options.mutable_ssl_options();
    ssl_options->sni_name = server_address;
    ssl_options->verify.verify_depth =
        1;  // Enable certificate verification, to disable SSL set it to 0
    ssl_options->verify.ca_file_path =
        path_to_cert;  // Path to your downloaded TLS certificate
  }
  CouchbaseOperations::Result result;
  brpc::Channel* new_channel = new brpc::Channel();
  if (new_channel->Init(server_address.c_str(), &options) != 0) {
    DEBUG_PRINT("Failed to initialize Couchbase channel to " << server_address);
    delete new_channel;
    result.success = false;
    result.value = "";
    result.error_message = "Failed to initialize Couchbase channel";
    return result;
  }
  // Create a CouchbaseRequest and CouchbaseResponse for authentication
  CouchbaseRequest request;
  CouchbaseResponse response;
  brpc::Controller cntl;
  if (request.authenticateRequest(username.c_str(), password.c_str()) ==
      false) {
    DEBUG_PRINT("Failed to create Authenticate request for user: " << username);
    delete new_channel;
    result.success = false;
    return result;
  }
  new_channel->CallMethod(NULL, &cntl, &request, &response, NULL);
  if (cntl.Failed()) {
    DEBUG_PRINT("Failed to access Couchbase: " << cntl.ErrorText());
    delete new_channel;
    result.success = false;
    result.value = "";
    result.error_message = cntl.ErrorText();
    return result;
  }
  uint64_t cas;
  if (response.popHello(&cas) == false) {
    DEBUG_PRINT("Failed to receive HELO response from Couchbase: "
                << response.lastError());
    delete new_channel;
    result.success = false;
    result.value = "";
    result.error_message = response.lastError();
    result.status_code = response._status_code;
    return result;
  }
  if (response.popAuthenticate(&cas) == false) {
    DEBUG_PRINT("Failed to authenticate user: " << username << " to Couchbase: "
                                                << response.lastError());
    result.success = false;
    result.value = "";
    result.error_message = response.lastError();
    result.status_code = response._status_code;
    return result;
  }
  // Successfully authenticated
  channel_ = new_channel;
  this->server_address_ = server_address;
  result.success = true;
  result.status_code = 0;

  DEBUG_PRINT("Instance " << reinterpret_cast<uintptr_t>(this)
                          << " authenticated with unique connection_group:"
                          << server_address_ + bucket_name);

  // select the bucket
  result = selectBucket(bucket_name);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::selectBucket(
    const std::string& bucket_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  if (request.selectBucketRequest(bucket_name.c_str()) == false) {
    DEBUG_PRINT(
        "Failed to create Select Bucket request for bucket: " << bucket_name);
    result.success = false;
    result.value = "";
    return result;
  }
  channel_->CallMethod(NULL, &cntl, &request, &response, NULL);
  if (cntl.Failed()) {
    DEBUG_PRINT("Failed to select bucket: "
                << bucket_name << " from Couchbase: " << cntl.ErrorText());
    result.success = false;
    result.value = "";
    result.error_message = cntl.ErrorText();
    return result;
  }
  if (response.popSelectBucket(NULL) == false) {
    result.success = false;
    result.value = "";
    result.error_message = response.lastError();
    result.status_code = response._status_code;
    return result;
  }
  // Successfully selected the bucket
  selected_bucket_ = bucket_name;
  result.success = true;
  result.value = "";
  result.status_code = 0;

  // fetch the collection manifest for this bucket and store it in local cache
  if (request.local_collection_manifest_cache->find(bucket_name) ==
      request.local_collection_manifest_cache->end()) {
    // only fetch if not already present in the local cache
    CouchbaseManifestManager::CollectionManifest manifest;
    if (!common_metadata_tracking.getBucketToCollectionManifest(
            server_address_, bucket_name, &manifest)) {
      DEBUG_PRINT("Collection manifest for bucket: "
                  << bucket_name
                  << " not found in global cache, the local cache");

      // manifest for this bucket/server is not cached yet, will fetch it from
      // server now. refresh will also update the local cache with the fetched
      // manifest
      request.metadata_tracking->refreshCollectionManifest(
          channel_, server_address_, bucket_name,
          request.local_collection_manifest_cache);
      // We simply try once to prefetch the manifest, before any collection
      // operation. If it fails, it will be lazily updated when a collection
      // operation is performed.
    } else {
      // update the local cache with the cache manifest from global
      // cache(common_metadata_tracking)
      DEBUG_PRINT("Updated local cache collection manifest for bucket: "
                  << bucket_name);
      (*request.local_collection_manifest_cache)[bucket_name] = manifest;
    }
  } else {
    DEBUG_PRINT("Collection manifest for bucket: "
                << bucket_name << " already present in local cache");
  }
  DEBUG_PRINT("Bucket selected successfully " << bucket_name);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::append(
    const std::string& key, const std::string& value, std::string collection_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  sendRequest(CouchbaseOperations::APPEND, key, value, collection_name, &result,
              channel_, server_address_, selected_bucket_, &request, &response);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::prepend(
    const std::string& key, const std::string& value, std::string collection_name) {
  CouchbaseRequest request(&local_bucket_to_collection_manifest_);
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  sendRequest(CouchbaseOperations::PREPEND, key, value, collection_name,
              &result, channel_, server_address_, selected_bucket_, &request,
              &response);
  return result;
}

CouchbaseOperations::Result CouchbaseOperations::version() {
  CouchbaseRequest request;
  CouchbaseResponse response;
  brpc::Controller cntl;
  CouchbaseOperations::Result result;
  if (request.versionRequest() == false) {
    DEBUG_PRINT("Failed to create Version request");
    result.success = false;
    result.value = "";
    return result;
  }
  channel_->CallMethod(NULL, &cntl, &request, &response, NULL);
  if (cntl.Failed()) {
    DEBUG_PRINT("Failed to get version from Couchbase: " << cntl.ErrorText());
    result.success = false;
    result.value = "";
    result.error_message = cntl.ErrorText();
    return result;
  }
  std::string version;
  if (response.popVersion(&version) == false) {
    result.success = false;
    result.value = "";
    result.error_message = response.lastError();
    result.status_code = response._status_code;
    return result;
  }
  // Successfully got version
  result.success = true;
  result.value = version;
  result.status_code = 0;
  return result;
}

bool CouchbaseOperations::beginPipeline() {
  if (pipeline_active) {
    DEBUG_PRINT("Pipeline already active. Call clearPipeline() first.");
    return false;
  }

  // Clear any previous state
  while (!pipeline_operations_queue.empty()) {
    pipeline_operations_queue.pop();
  }
  pipeline_request_couchbase_req.Clear();

  pipeline_active = true;
  return true;
}

bool CouchbaseOperations::pipelineRequest(operation_type op_type,
                                          const std::string& key,
                                          const std::string& value,
                                          std::string collection_name) {
  if (!pipeline_active) {
    DEBUG_PRINT("Pipeline not active. Call beginPipeline() first.");
    return false;
  }

  switch (op_type) {
    case GET:
      if (pipeline_request_couchbase_req.getRequest(
              key, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(GET);
      break;
    case UPSERT:
      if (pipeline_request_couchbase_req.upsertRequest(
              key, value, 0, 0, 0, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(UPSERT);
      break;
    case ADD:
      if (pipeline_request_couchbase_req.addRequest(
              key, value, 0, 0, 0, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(ADD);
      break;
    case APPEND:
      if (pipeline_request_couchbase_req.appendRequest(
              key, value, 0, 0, 0, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(APPEND);
      break;
    case PREPEND:
      if (pipeline_request_couchbase_req.prependRequest(
              key, value, 0, 0, 0, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(PREPEND);
      break;
    case DELETE:
      if (pipeline_request_couchbase_req.deleteRequest(
              key, collection_name, channel_, server_address_,
              selected_bucket_) == false) {
        return false;
      }
      pipeline_operations_queue.push(DELETE);
      break;
    default:
      DEBUG_PRINT("Invalid operation type for pipelining");
      return false;
  }
  return true;
}
std::vector<CouchbaseOperations::Result> CouchbaseOperations::executePipeline() {
  std::vector<CouchbaseOperations::Result> results;

  if (!pipeline_active || pipeline_operations_queue.empty()) {
    DEBUG_PRINT("No pipeline active or no operations queued");
    return results;
  }

  brpc::Controller cntl;
  channel_->CallMethod(NULL, &cntl, &pipeline_request_couchbase_req,
                       &pipeline_response_couchbase_resp, NULL);

  if (cntl.Failed()) {
    DEBUG_PRINT("Pipeline execution failed: " << cntl.ErrorText());
    // Create failure results for all operations
    size_t op_count = pipeline_operations_queue.size();
    results.reserve(op_count);

    CouchbaseOperations::Result failure_result;
    failure_result.success = false;
    failure_result.error_message = cntl.ErrorText();

    for (size_t i = 0; i < op_count; ++i) {
      results.push_back(failure_result);
    }

    clearPipeline();
    return results;
  }

  // Process each operation in the order they were added
  CouchbaseOperations::CouchbaseResponse* response =
      &pipeline_response_couchbase_resp;
  while (!pipeline_operations_queue.empty()) {
    CouchbaseOperations::Result result;
    operation_type op_type = pipeline_operations_queue.front();
    pipeline_operations_queue.pop();
    switch (op_type) {
      case GET: {
        std::string value;
        uint32_t flags = 0;
        uint64_t cas = 0;
        if (response->popGet(&value, &flags, &cas) == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = value;
        }
        results.push_back(result);
        break;
      }
      case UPSERT: {
        if (response->popUpsert(NULL) == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = "";
        }
        results.push_back(result);
        break;
      }
      case ADD: {
        if (response->popAdd(NULL) == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = "";
        }
        results.push_back(result);
        break;
      }
      case APPEND: {
        uint64_t cas_value;
        if (response->popAppend(&cas_value) == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = "";
        }
        results.push_back(result);
        break;
      }
      case PREPEND: {
        uint64_t cas_value;
        if (response->popPrepend(&cas_value) == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = "";
        }
        results.push_back(result);
        break;
      }
      case DELETE: {
        if (response->popDelete() == false) {
          result.success = false;
          result.value = "";
          result.error_message = response->lastError();
          result.status_code = response->_status_code;
        } else {
          result.success = true;
          result.value = "";
        }
        results.push_back(result);
        break;
      }
      default:
        DEBUG_PRINT("Invalid operation type in pipeline response processing");
        result.success = false;
        result.value = "";
        result.error_message = "Invalid operation type";
        results.push_back(result);
        break;
    }
  }

  pipeline_active = false;
  pipeline_request_couchbase_req.Clear();

  return results;
}

bool CouchbaseOperations::clearPipeline() {
  while (!pipeline_operations_queue.empty()) {
    pipeline_operations_queue.pop();
  }
  pipeline_request_couchbase_req.Clear();
  pipeline_active = false;
  return true;
}
}  // namespace brpc