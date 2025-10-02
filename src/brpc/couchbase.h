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
#include <queue>

#include "brpc/nonreflectable_message.h"
#include <brpc/channel.h>
#include "brpc/pb_compat.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
using namespace std;

namespace google { namespace protobuf { class Message; } }

namespace brpc {

// Forward declarations for friend functions
class InputMessageBase;
class Controller;
namespace policy {
  void ProcessCouchbaseResponse(InputMessageBase* msg);
  void SerializeCouchbaseRequest(butil::IOBuf* buf, Controller* cntl, const google::protobuf::Message* request);
}

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
    ReaderWriterLock() : reader_count_(0), writer_active_(false), waiting_writers_(0) {}

    void lock_shared() {
        std::unique_lock<std::mutex> lock(mutex_);
        reader_cv_.wait(lock, [this] { return !writer_active_.load() && waiting_writers_.load() == 0; });
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
        writer_cv_.wait(lock, [this] { return !writer_active_.load() && reader_count_.load() == 0; });
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
    ~SharedLock() {
        lock_.unlock_shared();
    }
};

class UniqueLock {
private:
    ReaderWriterLock& lock_;
public:
    explicit UniqueLock(ReaderWriterLock& lock) : lock_(lock) {
        lock_.lock();
    }
    ~UniqueLock() {
        lock_.unlock();
    }
};

class CouchbaseMetadataTracking{
  public:
    struct CollectionManifest{
      string uid; //uid of the manifest, it can be used to track if the manifest is updated
      unordered_map<string, unordered_map<string, uint8_t>> scope_to_collectionID_map;  //scope -> (collection -> collection_id)
    };
  private:
    unordered_map<string /*server*/, unordered_map<string /*bucket*/, CollectionManifest>> bucket_to_collection_manifest;
    ReaderWriterLock rw_bucket_to_collection_manifest_mutex;

  public:
    CouchbaseMetadataTracking() {}
    ~CouchbaseMetadataTracking() {
      bucket_to_collection_manifest.clear();
    }
    bool set_bucket_to_collection_manifest(string server, string bucket, CollectionManifest manifest);
    
    bool get_bucket_to_collection_manifest(string server, string bucket, CollectionManifest *manifest);
    bool get_manifest_to_collection_id(CollectionManifest *manifest, string scope, string collection, uint8_t *collection_id);

    bool json_to_collection_manifest(const string& json, CollectionManifest *manifest);
} static common_metadata_tracking;
class CouchbaseOperations {
  public:
    enum pipeline_operation_type {
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
      string error_message;
      string value;
    };
    Result Get(const string& key, string collection_name = "_default");
    Result Upsert(const string& key, const string& value, string collection_name = "_default");
    Result Add(const string& key, const string& value, string collection_name = "_default");
    // Warning: Not tested
    // Result Replace(const string& key, const string& value, string collection_name = "_default");
    Result Append(const string& key, const string& value, string collection_name = "_default");
    Result Prepend(const string& key, const string& value, string collection_name = "_default");
    Result Delete(const string& key, string collection_name = "_default");
    // Warning: Not tested
    // Result Increment(const string& key, uint64_t delta, uint64_t initial_value, uint32_t exptime, string collection_name = "_default");
    // Result Decrement(const string& key, uint64_t delta, uint64_t initial_value, uint32_t exptime, string collection_name = "_default");
    // Result Touch(const string& key, uint32_t exptime, string collection_name = "_default");
    // Result Flush(uint32_t timeout = 0);
    Result Version();
    Result Authenticate(const string& username, const string& password, const string& server_address, bool enable_ssl, string path_to_cert);
    Result SelectBucket(const string& bucket_name);
    
    // Pipeline management
    bool BeginPipeline();
    bool PipelineRequest(pipeline_operation_type op_type, const string& key, const string& value = "", string collection_name = "_default");
    vector<Result> ExecutePipeline();  // Return by value instead of pointer
    bool ClearPipeline();
    
    // Pipeline status
    bool IsPipelineActive() const { return pipeline_active; }
    size_t GetPipelineSize() const { return pipeline_operations_queue.size(); }
    
    CouchbaseOperations() : pipeline_active(false) {}
    ~CouchbaseOperations() {}
  private:
    // Friend functions to allow access to private nested classes
    friend bool get_cached_or_fetch_collection_id(string collection_name, uint8_t *coll_id, brpc::CouchbaseMetadataTracking *metadata_tracking, brpc::Channel* channel, const string& server, const string& selected_bucket);
    friend void policy::ProcessCouchbaseResponse(InputMessageBase* msg);
    friend void policy::SerializeCouchbaseRequest(butil::IOBuf* buf, Controller* cntl, const google::protobuf::Message* request);

    brpc::Channel* channel;
    string server_address;
    string selected_bucket;

    class CouchbaseRequest : public NonreflectableMessage<CouchbaseRequest> {
      private:
        static brpc::CouchbaseMetadataTracking *metadata_tracking;
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
        CouchbaseRequest() : NonreflectableMessage<CouchbaseRequest>() {
          metadata_tracking = &common_metadata_tracking;
          SharedCtor();
        }
        ~CouchbaseRequest() { SharedDtor(); }
        CouchbaseRequest(const CouchbaseRequest& from) : NonreflectableMessage<CouchbaseRequest>(from) {
          SharedCtor();
          MergeFrom(from);
        }

        inline CouchbaseRequest& operator=(const CouchbaseRequest& from) {
          CopyFrom(from);
          return *this;
        }

        bool SelectBucketRequest(const butil::StringPiece& bucket_name);
        bool AuthenticateRequest(const butil::StringPiece& username,
                          const butil::StringPiece& password);
        bool HelloRequest();

        // Using GetCollectionManifest instead of fetching collection ID directly
        // bool GetCollectionId(const butil::StringPiece& scope_name,
        //                      const butil::StringPiece& collection_name);

        bool GetScopeId(const butil::StringPiece& scope_name);

        bool GetCollectionManifest();

        bool RefreshCollectionManifest();

        // Collection-aware document operations
        bool GetRequest(const butil::StringPiece& key, string collection_name = "_default", 
                      brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool UpsertRequest(const butil::StringPiece& key, const butil::StringPiece& value,
                    uint32_t flags, uint32_t exptime, uint64_t cas_value,
                    string collection_name = "_default",
                    brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool AddRequest(const butil::StringPiece& key, const butil::StringPiece& value,
                uint32_t flags, uint32_t exptime, uint64_t cas_value,
                string collection_name = "_default",
                brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        // Warning: Not tested
        // bool ReplaceRequest(const butil::StringPiece& key, const butil::StringPiece& value,
        //             uint32_t flags, uint32_t exptime, uint64_t cas_value,
        //             string collection_name = "_default",
        //             brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool AppendRequest(const butil::StringPiece& key, const butil::StringPiece& value,
                    uint32_t flags, uint32_t exptime, uint64_t cas_value,
                    string collection_name = "_default",
                    brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool PrependRequest(const butil::StringPiece& key, const butil::StringPiece& value,
                    uint32_t flags, uint32_t exptime, uint64_t cas_value,
                    string collection_name = "_default",
                    brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool DeleteRequest(const butil::StringPiece& key, string collection_name = "_default",
                          brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");
        
        // Warning: Not tested
        // bool FlushRequest(uint32_t timeout);

        // bool IncrementRequest(const butil::StringPiece& key, uint64_t delta,
        //               uint64_t initial_value, uint32_t exptime, string collection_name = "_default",
        //               brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");
        // bool DecrementRequest(const butil::StringPiece& key, uint64_t delta,
        //               uint64_t initial_value, uint32_t exptime, string collection_name = "_default",
        //               brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        // bool TouchRequest(const butil::StringPiece& key, uint32_t exptime,
        //           string collection_name = "_default",
        //           brpc::Channel* channel = nullptr, const string& server = "", const string& bucket = "");

        bool VersionRequest();

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
        static brpc::CouchbaseMetadataTracking *metadata_tracking;
        butil::IOBuf _buf;
        mutable int _cached_size_;
        bool PopCounter(uint8_t command, uint64_t* new_value, uint64_t* cas_value);
        bool PopStore(uint8_t command, uint64_t* cas_value);

        void SharedCtor();
        void SharedDtor();
        void SetCachedSize(int size) const PB_425_OVERRIDE;

      public:
        CouchbaseResponse() : NonreflectableMessage<CouchbaseResponse>() {
          SharedCtor();
        }
        ~CouchbaseResponse() { SharedDtor(); }
        CouchbaseResponse(const CouchbaseResponse& from) : NonreflectableMessage<CouchbaseResponse>(from) {
          metadata_tracking = &common_metadata_tracking;
          SharedCtor();
          MergeFrom(from);
        }
        inline CouchbaseResponse& operator=(const CouchbaseResponse& from) {
          CopyFrom(from);
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
        // Warning: Not tested
        // bool PopReplace(uint64_t* cas_value);
        bool PopAppend(uint64_t* cas_value);
        bool PopPrepend(uint64_t* cas_value);
        bool PopSelectBucket(uint64_t* cas_value, std::string bucket_name);

        // Collection-related response methods
        bool PopCollectionId(uint8_t* collection_id);

        bool PopManifest(std::string* manifest_json);

        bool PopDelete();
        // Warning: Not tested
        // bool PopFlush();
        // bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
        // bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
        // bool PopTouch();
        bool PopVersion(string* version);
    };

    // Pipeline management - per instance
    queue<pipeline_operation_type> pipeline_operations_queue;
    CouchbaseRequest pipeline_request;
    CouchbaseResponse pipeline_response;
    bool pipeline_active;
};

}  // namespace brpc