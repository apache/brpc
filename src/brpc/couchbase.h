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

#ifndef BRPC_COUCHBASE_H
#define BRPC_COUCHBASE_H

#endif

#include <string>

#include "brpc/nonreflectable_message.h"
#include <brpc/channel.h>
#include "brpc/pb_compat.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include <shared_mutex>
#include <unordered_map>
using namespace std;

namespace brpc {
class CouchbaseMetadataTracking{
  public:
    struct ChannelInfo {
      brpc::Channel* channel;
      string server;
      string selected_bucket;
    };

    struct CollectionManifest{
      string uid; //uid of the manifest, it can be used to track if the manifest is updated
      unordered_map<string, unordered_map<string, uint8_t>> scope_to_collectionID_map;  //scope -> (collection -> collection_id)
    };
  private:
    unordered_map<uint64_t /*thread_id*/, ChannelInfo> thread_to_channel_info;
    shared_mutex rw_thread_to_channel_info_mutex;

    unordered_map<string /*server*/, unordered_map<string /*bucket*/, CollectionManifest>> bucket_to_collection_manifest;
    shared_mutex rw_bucket_to_collection_manifest_mutex;

  public:
    CouchbaseMetadataTracking() {}
    ~CouchbaseMetadataTracking() {
      bucket_to_collection_manifest.clear();
      thread_to_channel_info.clear();
    }
    bool set_channel_info_for_thread(uint64_t thread_id, brpc::Channel* channel, const string& server);
    bool set_current_bucket_for_thread(uint64_t thread_id, const string& bucket);
    bool set_bucket_to_collection_manifest(uint64_t thread_id, CollectionManifest manifest);
    
    bool get_channel_info_for_thread(uint64_t thread_id, ChannelInfo *channel_info);
    bool get_bucket_to_collection_manifest(uint64_t thread_id, string server, string bucket, CollectionManifest *manifest);
    bool get_manifest_to_collection_id(CollectionManifest *manifest, string scope, string collection, uint8_t *collection_id);

    bool json_to_collection_manifest(const string& json, CollectionManifest *manifest);
} static common_metadata_tracking;

class CouchbaseRequest : public NonreflectableMessage<CouchbaseRequest> {
 private:
  static CouchbaseMetadataTracking *metadata_tracking;
  int _pipelined_count;
  butil::IOBuf _buf;
  mutable int _cached_size_;
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const PB_425_OVERRIDE;
  bool GetOrDelete(uint8_t command, const butil::StringPiece& key,
                   uint8_t coll_id = 0);
  bool Counter(uint8_t command, const butil::StringPiece& key, uint64_t delta,
               uint64_t initial_value, uint32_t exptime);

  bool Store(uint8_t command, const butil::StringPiece& key,
             const butil::StringPiece& value, uint32_t flags, uint32_t exptime,
             uint64_t cas_value, uint8_t coll_id = 0);
  uint32_t hash_crc32(const char* key, size_t key_length);

 public:
  CouchbaseRequest();
  ~CouchbaseRequest() override;
  CouchbaseRequest(const CouchbaseRequest& from);
  inline CouchbaseRequest& operator=(const CouchbaseRequest& from) {
    CopyFrom(from);
    return *this;
  }

  bool SelectBucket(const butil::StringPiece& bucket_name);
  bool Authenticate(const butil::StringPiece& username,
                    const butil::StringPiece& password,
                    brpc::Channel* channel,
                    const string server);
  bool HelloRequest();

  // Collection Management Method
  bool GetCollectionId(const butil::StringPiece& scope_name,
                       const butil::StringPiece& collection_name);

  bool GetScopeId(const butil::StringPiece& scope_name);

  bool GetCollectionManifest();

  // Collection-aware document operations
  bool Get(const butil::StringPiece& key, string collection_name = "_default");

  bool Upsert(const butil::StringPiece& key, const butil::StringPiece& value,
              uint32_t flags, uint32_t exptime, uint64_t cas_value,
              string collection_name = "_default");

  bool Add(const butil::StringPiece& key, const butil::StringPiece& value,
           uint32_t flags, uint32_t exptime, uint64_t cas_value,
           string collection_name = "_default");

  bool Replace(const butil::StringPiece& key, const butil::StringPiece& value,
               uint32_t flags, uint32_t exptime, uint64_t cas_value,
               string collection_name = "_default");

  bool Append(const butil::StringPiece& key, const butil::StringPiece& value,
              uint32_t flags, uint32_t exptime, uint64_t cas_value,
              string collection_name = "_default");

  bool Prepend(const butil::StringPiece& key, const butil::StringPiece& value,
               uint32_t flags, uint32_t exptime, uint64_t cas_value,
               string collection_name = "_default");

  bool Delete(const butil::StringPiece& key, string collection_name = "_default");
  bool Flush(uint32_t timeout);

  bool Increment(const butil::StringPiece& key, uint64_t delta,
                 uint64_t initial_value, uint32_t exptime, string collection_name = "_default");
  bool Decrement(const butil::StringPiece& key, uint64_t delta,
                 uint64_t initial_value, uint32_t exptime, string collection_name = "_default");

  bool Touch(const butil::StringPiece& key, uint32_t exptime,
             string collection_name = "_default");

  bool Version();

  int pipelined_count() const { return _pipelined_count; }

  butil::IOBuf& raw_buffer() { return _buf; }
  const butil::IOBuf& raw_buffer() const { return _buf; }
  void Swap(CouchbaseRequest* other);
  void MergeFrom(const CouchbaseRequest& from) override;
  void Clear() override;
  bool IsInitialized() const PB_527_OVERRIDE;
  size_t ByteSizeLong() const override;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input) PB_310_OVERRIDE;
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const PB_310_OVERRIDE;
  int GetCachedSize() const PB_425_OVERRIDE { return _cached_size_; }

  ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;
};

class CouchbaseResponse : public NonreflectableMessage<CouchbaseResponse> {
 private:
  string _err;
  static CouchbaseMetadataTracking *metadata_tracking;
  butil::IOBuf _buf;
  mutable int _cached_size_;
  bool PopCounter(uint8_t command, uint64_t* new_value, uint64_t* cas_value);
  bool PopStore(uint8_t command, uint64_t* cas_value);

  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const PB_425_OVERRIDE;

 public:
  CouchbaseResponse();
  ~CouchbaseResponse() override;
  CouchbaseResponse(const CouchbaseResponse& from);
  inline CouchbaseResponse& operator=(const CouchbaseResponse& from) {
    CopyFrom(from);
    return *this;
  }
  enum Status {
    STATUS_SUCCESS = 0x00,
    STATUS_KEY_ENOENT = 0x01,
    STATUS_KEY_EEXISTS = 0x02,
    STATUS_E2BIG = 0x03,
    STATUS_EINVAL = 0x04,
    STATUS_NOT_STORED = 0x05,
    STATUS_DELTA_BADVAL = 0x06,
    STATUS_VBUCKET_BELONGS_TO_ANOTHER_SERVER = 0x07,
    STATUS_AUTH_ERROR = 0x20,
    STATUS_AUTH_CONTINUE = 0x21,
    STATUS_ERANGE = 0x22,
    STATUS_ROLLBACK = 0x23,
    STATUS_EACCESS = 0x24,
    STATUS_NOT_INITIALIZED = 0x25,
    STATUS_UNKNOWN_COMMAND = 0x81,
    STATUS_ENOMEM = 0x82,
    STATUS_NOT_SUPPORTED = 0x83,
    STATUS_EINTERNAL = 0x84,
    STATUS_EBUSY = 0x85,
    STATUS_ETMPFAIL = 0x86,
    STATUS_UNKNOWN_COLLECTION = 0x88,
    STATUS_NO_COLLECTIONS_MANIFEST = 0x89,
    STATUS_CANNOT_APPLY_COLLECTIONS_MANIFEST = 0x8a,
    STATUS_COLLECTIONS_MANIFEST_IS_AHEAD = 0x8b,
    STATUS_UNKNOWN_SCOPE = 0x8c,
    STATUS_DCP_STREAM_ID_INVALID = 0x8d,
    STATUS_DURABILITY_INVALID_LEVEL = 0xa0,
    STATUS_DURABILITY_IMPOSSIBLE = 0xa1,
    STATUS_SYNC_WRITE_IN_PROGRESS = 0xa2,
    STATUS_SYNC_WRITE_AMBIGUOUS = 0xa3,
    STATUS_SYNC_WRITE_RE_COMMIT_IN_PROGRESS = 0xa4,
    STATUS_SUBDOC_PATH_NOT_FOUND = 0xc0,
    STATUS_SUBDOC_PATH_MISMATCH = 0xc1,
    STATUS_SUBDOC_PATH_EINVAL = 0xc2,
    STATUS_SUBDOC_PATH_E2BIG = 0xc3,
    STATUS_SUBDOC_DOC_E2DEEP = 0xc4,
    STATUS_SUBDOC_VALUE_CANTINSERT = 0xc5,
    STATUS_SUBDOC_DOC_NOT_JSON = 0xc6,
    STATUS_SUBDOC_NUM_E2BIG = 0xc7,
    STATUS_SUBDOC_DELTA_E2BIG = 0xc8,
    STATUS_SUBDOC_PATH_EEXISTS = 0xc9,
    STATUS_SUBDOC_VALUE_E2DEEP = 0xca,
    STATUS_SUBDOC_INVALID_COMBO = 0xcb,
    STATUS_SUBDOC_MULTI_PATH_FAILURE = 0xcc,
    STATUS_SUBDOC_SUCCESS_DELETED = 0xcd,
    STATUS_SUBDOC_XATTR_INVALID_FLAG_COMBO = 0xce,
    STATUS_SUBDOC_XATTR_INVALID_KEY_COMBO = 0xcf,
    STATUS_SUBDOC_XATTR_UNKNOWN_MACRO = 0xd0,
    STATUS_SUBDOC_XATTR_UNKNOWN_VATTR = 0xd1,
    STATUS_SUBDOC_XATTR_CANT_MODIFY_VATTR = 0xd2,
    STATUS_SUBDOC_MULTI_PATH_FAILURE_DELETED = 0xd3,
    STATUS_SUBDOC_INVALID_XATTR_ORDER = 0xd4,
    STATUS_SUBDOC_XATTR_UNKNOWN_VATTR_MACRO = 0xd5,
    STATUS_SUBDOC_CAN_ONLY_REVIVE_DELETED_DOCUMENTS = 0xd6,
    STATUS_SUBDOC_DELETED_DOCUMENT_CANT_HAVE_VALUE = 0xd7,
    STATUS_XATTR_EINVAL = 0xe0
  };
  const char* couchbase_binary_command_to_string(uint8_t cmd);
  void MergeFrom(const CouchbaseResponse& from) override;
  void Clear() override;
  bool IsInitialized() const PB_527_OVERRIDE;

  size_t ByteSizeLong() const override;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input) PB_310_OVERRIDE;
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const PB_310_OVERRIDE;
  int GetCachedSize() const PB_425_OVERRIDE { return _cached_size_; }

  ::google::protobuf::Metadata GetMetadata() const PB_527_OVERRIDE;

  butil::IOBuf& raw_buffer() { return _buf; }
  const butil::IOBuf& raw_buffer() const { return _buf; }
  static const char* status_str(Status);

  // Helper method to format error messages with status codes
  static string format_error_message(uint16_t status_code,
                                          const string& operation,
                                          const string& error_msg = "");

  // Add methods to handle response parsing
  void Swap(CouchbaseResponse* other);
  bool PopGet(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
  bool PopGet(string* value, uint32_t* flags, uint64_t* cas_value);
  const string& LastError() const { return _err; }
  bool PopUpsert(uint64_t* cas_value);
  bool PopAdd(uint64_t* cas_value);
  bool PopReplace(uint64_t* cas_value);
  bool PopAppend(uint64_t* cas_value);
  bool PopPrepend(uint64_t* cas_value);
  bool PopSelectBucket(uint64_t* cas_value, std::string bucket_name);

  // Collection-related response methods
  bool PopCollectionId(uint8_t* collection_id);

  bool PopManifest(std::string* manifest_json);

  bool PopDelete();
  bool PopFlush();
  bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
  bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
  bool PopTouch();
  bool PopVersion(string* version);
};
}  // namespace brpc