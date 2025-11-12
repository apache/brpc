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

#include <brpc/channel.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

#include "brpc/nonreflectable_message.h"
#include "brpc/pb_compat.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"

namespace brpc {

// Forward declarations for friend functions
class InputMessageBase;
class Controller;
namespace policy {
void ProcessCouchbaseResponse(InputMessageBase* msg);
void SerializeCouchbaseRequest(butil::IOBuf* buf, Controller* cntl,
                               const google::protobuf::Message* request);
}  // namespace policy

// Simple C++11 compatible reader-writer lock
class ReaderWriterLock {
 private:
  std::mutex mutex_;
  std::condition_variable reader_cv_;
  std::condition_variable writer_cv_;
  std::atomic<int> reader_count_;
  std::atomic<bool> writer_active_;
  std::atomic<int> waiting_writers_;

 public:
  ReaderWriterLock()
      : reader_count_(0), writer_active_(false), waiting_writers_(0) {}

  void lock_shared() {
    std::unique_lock<std::mutex> lock(mutex_);
    reader_cv_.wait(lock, [this] {
      return !writer_active_.load() && waiting_writers_.load() == 0;
    });
    reader_count_.fetch_add(1);
  }

  void unlock_shared() {
    reader_count_.fetch_sub(1);
    if (reader_count_.load() == 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      writer_cv_.notify_one();
    }
  }

  void lock() {
    std::unique_lock<std::mutex> lock(mutex_);
    waiting_writers_.fetch_add(1);
    writer_cv_.wait(lock, [this] {
      return !writer_active_.load() && reader_count_.load() == 0;
    });
    waiting_writers_.fetch_sub(1);
    writer_active_.store(true);
  }

  void unlock() {
    writer_active_.store(false);
    std::lock_guard<std::mutex> lock(mutex_);
    writer_cv_.notify_one();
    reader_cv_.notify_all();
  }
};

// RAII helper classes
class SharedLock {
 private:
  ReaderWriterLock& lock_;

 public:
  explicit SharedLock(ReaderWriterLock& lock) : lock_(lock) {
    lock_.lock_shared();
  }
  ~SharedLock() { lock_.unlock_shared(); }
};

class UniqueLock {
 private:
  ReaderWriterLock& lock_;

 public:
  explicit UniqueLock(ReaderWriterLock& lock) : lock_(lock) { lock_.lock(); }
  ~UniqueLock() { lock_.unlock(); }
};

// manager
class CouchbaseManifestManager {
 public:
  struct CollectionManifest {
    std::string uid;  // uid of the manifest, it can be used to track if the manifest
                 // is updated
    std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>>
        scope_to_collection_id_map;  // scope -> (collection -> collection_id)
  };

 private:
  std::unordered_map<std::string /*server*/,
                std::unordered_map<std::string /*bucket*/, CollectionManifest>>
      bucket_to_collection_manifest_;
  ReaderWriterLock rw_bucket_to_collection_manifest_mutex_;

 public:
  CouchbaseManifestManager() {}
  ~CouchbaseManifestManager() { bucket_to_collection_manifest_.clear(); }
  bool setBucketToCollectionManifest(std::string server, std::string bucket,
                                     CollectionManifest manifest);

  bool getBucketToCollectionManifest(std::string server, std::string bucket,
                                     CollectionManifest* manifest);
  bool getManifestToCollectionId(CollectionManifest* manifest, std::string scope,
                                 std::string collection, uint8_t* collection_id);

  bool jsonToCollectionManifest(const std::string& json,
                                CollectionManifest* manifest);
  bool refreshCollectionManifest(
      brpc::Channel* channel, const std::string& server, const std::string& bucket,
      std::unordered_map<std::string, CollectionManifest>* local_cache = nullptr);
} static common_metadata_tracking;
class CouchbaseOperations {
 public:
  enum operation_type {
    GET = 1,
    UPSERT = 2,
    ADD = 3,
    REPLACE = 4,
    APPEND = 5,
    PREPEND = 6,
    DELETE = 7
  };
  struct Result {
    bool success;
    std::string error_message;
    std::string value;
    uint16_t status_code;  // 0x00 if success
  };
  Result get(const std::string& key, std::string collection_name = "_default");
  Result upsert(const std::string& key, const std::string& value,
                std::string collection_name = "_default");
  Result add(const std::string& key, const std::string& value,
             std::string collection_name = "_default");
  // Warning: Not tested
  // Result replace(const std::string& key, const std::string& value, std::string
  // collection_name = "_default");
  Result append(const std::string& key, const std::string& value,
                std::string collection_name = "_default");
  Result prepend(const std::string& key, const std::string& value,
                 std::string collection_name = "_default");
  Result delete_(const std::string& key, std::string collection_name = "_default");
  // Warning: Not tested
  // Result Increment(const string& key, uint64_t delta, uint64_t initial_value,
  // uint32_t exptime, string collection_name = "_default"); Result
  // Decrement(const string& key, uint64_t delta, uint64_t initial_value,
  // uint32_t exptime, string collection_name = "_default"); Result Touch(const
  // string& key, uint32_t exptime, string collection_name = "_default"); Result
  // Flush(uint32_t timeout = 0);
  Result version();
  Result authenticateSSL(const std::string& username, const std::string& password,
                         const std::string& server_address,
                         const std::string& bucket_name, std::string path_to_cert = "");
  Result authenticate(const std::string& username, const std::string& password,
                      const std::string& server_address, const std::string& bucket_name);
  Result selectBucket(const std::string& bucket_name);

  // Pipeline management
  bool beginPipeline();
  bool pipelineRequest(operation_type op_type, const std::string& key,
                       const std::string& value = "",
                       std::string collection_name = "_default");
  std::vector<Result> executePipeline();  // Return by value instead of pointer
  bool clearPipeline();

  // Pipeline status
  bool isPipelineActive() const { return pipeline_active; }
  size_t getPipelineSize() const { return pipeline_operations_queue.size(); }

  CouchbaseOperations()
      : pipeline_request_couchbase_req(&local_bucket_to_collection_manifest_),
        pipeline_active(false) {}
  ~CouchbaseOperations() {}
  bool getLocalCachedCollectionId(const std::string& bucket, const std::string& scope,
                                  const std::string& collection, uint8_t* coll_id);

 private:
  CouchbaseOperations::Result authenticateAll(const std::string& username,
                                              const std::string& password,
                                              const std::string& server_address,
                                              const std::string& bucket_name,
                                              bool enable_ssl,
                                              std::string path_to_cert);
  friend void policy::ProcessCouchbaseResponse(InputMessageBase* msg);
  friend void policy::SerializeCouchbaseRequest(
      butil::IOBuf* buf, Controller* cntl,
      const google::protobuf::Message* request);
  brpc::Channel* channel_;
  std::string server_address_;
  std::string selected_bucket_;

  std::unordered_map<std::string /*bucket*/, CouchbaseManifestManager::CollectionManifest>
      local_bucket_to_collection_manifest_;

 public:
  // these classes have been made public so that normal user can also create
  // advanced bRPC programs as per their requirements.
  class CouchbaseRequest : public NonreflectableMessage<CouchbaseRequest> {
   public:
    static brpc::CouchbaseManifestManager* metadata_tracking;
    int _pipelined_count;
    butil::IOBuf _buf;
    mutable int _cached_size_;
    void sharedCtor();
    void sharedDtor();
    void setCachedSize(int size) const;
    bool getOrDelete(uint8_t command, const butil::StringPiece& key,
                     uint8_t coll_id = 0);
    bool counter(uint8_t command, const butil::StringPiece& key, uint64_t delta,
                 uint64_t initial_value, uint32_t exptime);

    bool store(uint8_t command, const butil::StringPiece& key,
               const butil::StringPiece& value, uint32_t flags,
               uint32_t exptime, uint64_t cas_value, uint8_t coll_id = 0);
    uint32_t hashCrc32(const char* key, size_t key_length);

   public:
    std::unordered_map<std::string /*bucket*/,
                  CouchbaseManifestManager::CollectionManifest>*
        local_collection_manifest_cache;

    CouchbaseRequest(
        std::unordered_map<std::string /*bucket*/,
                      CouchbaseManifestManager::CollectionManifest>*
            local_cache_reference)
        : NonreflectableMessage<CouchbaseRequest>() {
      metadata_tracking = &common_metadata_tracking;
      local_collection_manifest_cache = local_cache_reference;
      sharedCtor();
    }
    CouchbaseRequest() : NonreflectableMessage<CouchbaseRequest>() {
      metadata_tracking = &common_metadata_tracking;
      sharedCtor();
    }
    ~CouchbaseRequest() { sharedDtor(); }
    CouchbaseRequest(const CouchbaseRequest& from)
        : NonreflectableMessage<CouchbaseRequest>() {
      metadata_tracking = &common_metadata_tracking;
      sharedCtor();
      MergeFrom(from);
    }

    inline CouchbaseRequest& operator=(const CouchbaseRequest& from) {
      if (this != &from) {
        MergeFrom(from);
      }
      return *this;
    }

    bool selectBucketRequest(const butil::StringPiece& bucket_name);
    bool authenticateRequest(const butil::StringPiece& username,
                             const butil::StringPiece& password);
    bool helloRequest();

    // Using GetCollectionManifest instead of fetching collection ID directly
    // bool GetCollectionId(const butil::StringPiece& scope_name,
    //                      const butil::StringPiece& collection_name);

    bool getScopeId(const butil::StringPiece& scope_name);

    bool getCollectionManifest();
      
    bool getLocalCachedCollectionId(const std::string& bucket, const std::string& scope,
                                    const std::string& collection, uint8_t* coll_id);

    bool getCachedOrFetchCollectionId(
        std::string collection_name, uint8_t* coll_id,
        brpc::CouchbaseManifestManager* metadata_tracking,
        brpc::Channel* channel, const std::string& server,
        const std::string& selected_bucket,
        std::unordered_map<std::string, CouchbaseManifestManager::CollectionManifest>*
            local_cache);

    // Collection-aware document operations
    bool getRequest(const butil::StringPiece& key,
                    std::string collection_name = "_default",
                    brpc::Channel* channel = nullptr, const std::string& server = "",
                    const std::string& bucket = "");

    bool upsertRequest(const butil::StringPiece& key,
                       const butil::StringPiece& value, uint32_t flags,
                       uint32_t exptime, uint64_t cas_value,
                       std::string collection_name = "_default",
                       brpc::Channel* channel = nullptr,
                       const std::string& server = "", const std::string& bucket = "");

    bool addRequest(const butil::StringPiece& key,
                    const butil::StringPiece& value, uint32_t flags,
                    uint32_t exptime, uint64_t cas_value,
                    std::string collection_name = "_default",
                    brpc::Channel* channel = nullptr, const std::string& server = "",
                    const std::string& bucket = "");

    bool appendRequest(const butil::StringPiece& key,
                       const butil::StringPiece& value, uint32_t flags,
                       uint32_t exptime, uint64_t cas_value,
                       std::string collection_name = "_default",
                       brpc::Channel* channel = nullptr,
                       const std::string& server = "", const std::string& bucket = "");

    bool prependRequest(const butil::StringPiece& key,
                        const butil::StringPiece& value, uint32_t flags,
                        uint32_t exptime, uint64_t cas_value,
                        std::string collection_name = "_default",
                        brpc::Channel* channel = nullptr,
                        const std::string& server = "", const std::string& bucket = "");

    bool deleteRequest(const butil::StringPiece& key,
                       std::string collection_name = "_default",
                       brpc::Channel* channel = nullptr,
                       const std::string& server = "", const std::string& bucket = "");

    bool versionRequest();

    int pipelinedCount() const { return _pipelined_count; }

    butil::IOBuf& rawBuffer() { return _buf; }
    const butil::IOBuf& rawBuffer() const {
      return _buf;
    }  // used in couchbase_protocol serialization.
    void Swap(CouchbaseRequest* other);
    void MergeFrom(const CouchbaseRequest& from) override;
    void Clear() override;
    bool IsInitialized() const PB_527_OVERRIDE;
  };

  class CouchbaseResponse : public NonreflectableMessage<CouchbaseResponse> {
   public:
    static brpc::CouchbaseManifestManager* metadata_tracking;

   private:
    std::string _err;
    butil::IOBuf _buf;
    mutable int _cached_size_;
    bool popCounter(uint8_t command, uint64_t* new_value, uint64_t* cas_value);
    bool popStore(uint8_t command, uint64_t* cas_value);

    void sharedCtor();
    void sharedDtor();
    void setCachedSize(int size) const;

   public:
    uint16_t _status_code;

    CouchbaseResponse() : NonreflectableMessage<CouchbaseResponse>() {
      sharedCtor();
    }
    ~CouchbaseResponse() { sharedDtor(); }
    CouchbaseResponse(const CouchbaseResponse& from)
        : NonreflectableMessage<CouchbaseResponse>() {
      metadata_tracking = &common_metadata_tracking;
      sharedCtor();
      MergeFrom(from);
    }
    inline CouchbaseResponse& operator=(const CouchbaseResponse& from) {
      if (this != &from) {
        MergeFrom(from);
      }
      return *this;
    }

    // the status codes are from Couchbase Binary Protocol documentation,
    // for original reference of status codes visit
    // https://github.com/couchbase/kv_engine/blob/master/include/mcbp/protocol/status.h
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
    const char* couchbaseBinaryCommandToString(uint8_t cmd);
    void MergeFrom(const CouchbaseResponse& from) override;
    void Clear() override;
    bool IsInitialized() const PB_527_OVERRIDE;

    butil::IOBuf& rawBuffer() { return _buf; }
    static const char* statusStr(Status);

    // Helper method to format error messages with status codes
    static std::string formatErrorMessage(uint16_t status_code,
                                     const std::string& operation,
                                     const std::string& error_msg = "");

    // Add methods to handle response parsing
    void swap(CouchbaseResponse* other);
    bool popGet(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
    bool popGet(std::string* value, uint32_t* flags, uint64_t* cas_value);
    const std::string& lastError() const { return _err; }
    bool popUpsert(uint64_t* cas_value);
    bool popAdd(uint64_t* cas_value);
    // Warning: Not tested
    // bool popReplace(uint64_t* cas_value);
    bool popAppend(uint64_t* cas_value);
    bool popPrepend(uint64_t* cas_value);
    bool popSelectBucket(uint64_t* cas_value);
    bool popAuthenticate(uint64_t* cas_value);
    bool popHello(uint64_t* cas_value);

    // Collection-related response methods
    bool popCollectionId(uint8_t* collection_id);

    bool popManifest(std::string* manifest_json);

    bool popDelete();
    // Warning: Not tested
    // bool popFlush();
    // bool popIncrement(uint64_t* new_value, uint64_t* cas_value);
    // bool popDecrement(uint64_t* new_value, uint64_t* cas_value);
    // bool popTouch();
    bool popVersion(std::string* version);
  };

  friend bool sendRequest(CouchbaseOperations::operation_type op_type,
                          const std::string& key, const std::string& value,
                          std::string collection_name,
                          CouchbaseOperations::Result* result,
                          brpc::Channel* channel, const std::string& server,
                          const std::string& bucket, CouchbaseRequest* request,
                          CouchbaseResponse* response);

  // Pipeline management - per instance
  std::queue<operation_type> pipeline_operations_queue;
  CouchbaseRequest pipeline_request_couchbase_req;
  CouchbaseResponse pipeline_response_couchbase_resp;
  bool pipeline_active;
};

}  // namespace brpc