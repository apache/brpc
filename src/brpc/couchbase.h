#ifndef BRPC_COUCHBASE_H
#define BRPC_COUCHBASE_H

#endif // COUCHBASE_MEMCACHE_H

#include <string>
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "brpc/nonreflectable_message.h"
#include "brpc/pb_compat.h"

namespace brpc {
    static uint32_t hash_crc32(const char *key, size_t key_length);
    class CouchbaseRequest : public NonreflectableMessage<CouchbaseRequest> {
        private:
            int _pipelined_count;
            butil::IOBuf _buf;
            mutable int _cached_size_;
            void SharedCtor();
            void SharedDtor();
            void SetCachedSize(int size) const PB_425_OVERRIDE;
            bool GetOrDelete(uint8_t command, const butil::StringPiece& key);
            bool Counter(uint8_t command, const butil::StringPiece& key, uint64_t delta,
                        uint64_t initial_value, uint32_t exptime);
    
            bool Store(uint8_t command, const butil::StringPiece& key,
                    const butil::StringPiece& value,
                    uint32_t flags, uint32_t exptime, uint64_t cas_value);
        public:
            CouchbaseRequest();
            ~CouchbaseRequest() override;
            CouchbaseRequest(const CouchbaseRequest& from);
            inline CouchbaseRequest& operator=(const CouchbaseRequest& from) {
                CopyFrom(from);
                return *this;
            }
            
            bool SelectBucket(const butil::StringPiece &bucket_name);
            bool Authenticate(const butil::StringPiece &username,
                                const butil::StringPiece &password);
            bool HelloRequest();
            
            // Collection Management Methods
            bool GetCollectionsManifest();
            bool GetCollectionId(const butil::StringPiece& scope_name, 
                               const butil::StringPiece& collection_name);

            bool GetScopeId(const butil::StringPiece& scope_name);
            
            // Collection-aware document operations
            bool Get(const butil::StringPiece& key);
            bool GetFromCollection(const butil::StringPiece& key, 
                                 const butil::StringPiece& scope_name,
                                 const butil::StringPiece& collection_name);
            
            bool Upsert(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value);
            bool UpsertToCollection(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value,
             const butil::StringPiece& scope_name, const butil::StringPiece& collection_name);
                     
            bool Add(const butil::StringPiece& key, const butil::StringPiece& value,
                    uint32_t flags, uint32_t exptime, uint64_t cas_value);

            bool Replace(const butil::StringPiece& key, const butil::StringPiece& value,
                        uint32_t flags, uint32_t exptime, uint64_t cas_value);
            
            bool Append(const butil::StringPiece& key, const butil::StringPiece& value,
                        uint32_t flags, uint32_t exptime, uint64_t cas_value);

            bool Prepend(const butil::StringPiece& key, const butil::StringPiece& value,
                        uint32_t flags, uint32_t exptime, uint64_t cas_value);

            bool Delete(const butil::StringPiece& key);
            bool Flush(uint32_t timeout);

            bool Increment(const butil::StringPiece& key, uint64_t delta,
                        uint64_t initial_value, uint32_t exptime);
            bool Decrement(const butil::StringPiece& key, uint64_t delta,
                        uint64_t initial_value, uint32_t exptime);
            
            bool Touch(const butil::StringPiece& key, uint32_t exptime);

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
            std::string _err;
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
            static std::string format_error_message(uint16_t status_code, const std::string& operation, const std::string& error_msg = "");
            
            // Add methods to handle response parsing
            void Swap(CouchbaseResponse* other);
            bool PopGet(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
            bool PopGet(std::string* value, uint32_t* flags, uint64_t* cas_value);
            const std::string& LastError() const { return _err; }
            bool PopUpsert(uint64_t* cas_value);
            bool PopAdd(uint64_t* cas_value);
            bool PopReplace(uint64_t* cas_value);
            bool PopAppend(uint64_t* cas_value);
            bool PopPrepend(uint64_t* cas_value);
            
            // Collection-related response methods
            bool PopCollectionsManifest(std::string* manifest_json);
            bool PopCollectionId(uint32_t* collection_id);
            
            // Collection-aware response methods (same as regular but for documentation)
            bool PopGetFromCollection(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
            bool PopUpsertToCollection(uint64_t* cas_value);

            bool PopDelete();
            bool PopFlush();
            bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
            bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
            bool PopTouch();
            bool PopVersion(std::string* version);
    };
}