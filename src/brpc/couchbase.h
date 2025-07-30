#ifndef BRPC_COUCHBASE_H
#define BRPC_COUCHBASE_H

#endif // COUCHBASE_MEMCACHE_H

#include <string>
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"
#include "brpc/nonreflectable_message.h"
#include "brpc/pb_compat.h"

namespace brpc {
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
            
            bool Set(const butil::StringPiece& key, const butil::StringPiece& value,
             uint32_t flags, uint32_t exptime, uint64_t cas_value);
            bool SetToCollection(const butil::StringPiece& key, const butil::StringPiece& value,
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
                STATUS_AUTH_ERROR = 0x20,
                STATUS_AUTH_CONTINUE = 0x21,
                STATUS_UNKNOWN_COMMAND = 0x81,
                STATUS_ENOMEM = 0x82
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
            // Add methods to handle response parsing
            void Swap(CouchbaseResponse* other);
            bool PopGet(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
            bool PopGet(std::string* value, uint32_t* flags, uint64_t* cas_value);
            const std::string& LastError() const { return _err; }
            bool PopSet(uint64_t* cas_value);
            bool PopAdd(uint64_t* cas_value);
            bool PopReplace(uint64_t* cas_value);
            bool PopAppend(uint64_t* cas_value);
            bool PopPrepend(uint64_t* cas_value);
            
            // Collection-related response methods
            bool PopCollectionsManifest(std::string* manifest_json);
            bool PopCollectionId(uint32_t* collection_id);
            
            // Collection-aware response methods (same as regular but for documentation)
            bool PopGetFromCollection(butil::IOBuf* value, uint32_t* flags, uint64_t* cas_value);
            bool PopSetToCollection(uint64_t* cas_value);

            bool PopDelete();
            bool PopFlush();
            bool PopIncrement(uint64_t* new_value, uint64_t* cas_value);
            bool PopDecrement(uint64_t* new_value, uint64_t* cas_value);
            bool PopTouch();
            bool PopVersion(std::string* version);
    };
}