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

#ifndef BRPC_MONGO_H
#define BRPC_MONGO_H

#include <bson/bson.h>
#include <google/protobuf/message.h>

#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "brpc/callback.h"
#include "brpc/mongo_head.h"
#include "brpc/parse_result.h"
#include "brpc/policy/mongo.pb.h"
#include "brpc/proto_base.pb.h"
#include "brpc/socket.h"
#include "butil/arena.h"
#include "butil/bson_util.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"

namespace brpc {

using butil::bson::BsonPtr;

struct MongoReply {
  int32_t response_flags;
  int64_t cursorid;
  int32_t straring_from;
  int32_t number_returned;
  std::vector<BsonPtr> documents;
};

struct DocumentSequence {
  mutable int32_t size;
  std::string identifier;
  std::vector<BsonPtr> documents;
  bool SerializeTo(butil::IOBuf* buf) const;
};

typedef std::shared_ptr<DocumentSequence> DocumentSequencePtr;

struct Section {
  uint8_t type;
  BsonPtr body_document;
  DocumentSequencePtr document_sequence;
  bool SeralizeTo(butil::IOBuf* buf) const;
};

struct MongoMsg {
  uint32_t flagbits;
  std::vector<Section> sections;
  uint32_t checksum;

  void make_host_endian() {
    if (!ARCH_CPU_LITTLE_ENDIAN) {
      flagbits = butil::ByteSwap(flagbits);
      checksum = butil::ByteSwap(checksum);
    }
  }

  bool checksumPresent() { return flagbits & 0x00000001; }
};

struct ReplicaSetMember {
  int32_t id;
  std::string addr;
  bool health;
  int32_t state;
  std::string state_str;
};

class MongoRequest : public ::google::protobuf::Message {
public:
    MongoRequest() = default;
    virtual ~MongoRequest() = default;
    MongoRequest(const MongoRequest& from) = default;
    MongoRequest& operator=(const MongoRequest &from) = default;

    static const ::google::protobuf::Descriptor* descriptor();
    virtual bool SerializeTo(butil::IOBuf* buf) const = 0;
    virtual const char* RequestId() const = 0;
    
protected:
    ::google::protobuf::Metadata GetMetadata() const override;
};

class MongoResponse : public ::google::protobuf::Message {
public:
    MongoResponse() = default;
    virtual~MongoResponse() = default;
    MongoResponse(const MongoResponse& from) = default;
    MongoResponse& operator=(const MongoResponse &from) = default;

    static const ::google::protobuf::Descriptor* descriptor();

protected:
    ::google::protobuf::Metadata GetMetadata() const override;
};

class MongoQueryRequest : public MongoRequest {
public:
    MongoQueryRequest();
    virtual ~MongoQueryRequest();
    MongoQueryRequest(const MongoQueryRequest& from);
    MongoQueryRequest& operator=(const MongoQueryRequest& from);
    void Swap(MongoQueryRequest* other);
    bool SerializeTo(butil::IOBuf* buf) const override;
    const char* RequestId() const override {
        return "query";
    }
    MongoQueryRequest* New() const;
    void CopyFrom(const ::google::protobuf::Message& from);
    void MergeFrom(const ::google::protobuf::Message& from);
    void CopyFrom(const MongoQueryRequest& from);
    void MergeFrom(const MongoQueryRequest& from);
    void Clear();
    bool IsInitialized() const;
    bool MergePartialFromCodedStream(
        ::google::protobuf::io::CodedInputStream* input);
    void SerializeWithCachedSizes(
        ::google::protobuf::io::CodedOutputStream* output) const;
    ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
        ::google::protobuf::uint8* output) const;
    int GetCachedSize() const { return _cached_size_; }
    static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // database
public:
    static const int kdatabaseFieldNumber = 1;
    const std::string& database() const { return database_; }
    bool has_database() const { return _has_bits_[0] & 0x1u; }
    void clear_database() {
      clear_has_database();
      database_.clear();
    }
    void set_database(std::string value) {
      database_ = value;
      set_has_database();
    }

private:
    void set_has_database() { _has_bits_[0] |= 0x1u; }
    void clear_has_database() { _has_bits_[0] &= ~0x1u; }

    std::string database_;

    // collection
public:
    static const int kcollectionFieldNumber = 2;
    const std::string& collection() const { return collection_; }
    bool has_collection() const { return _has_bits_[0] & 0x2u; }
    void clear_collection() {
      clear_has_collection();
      collection_.clear();
    }
    void set_collection(std::string value) {
      collection_ = value;
      set_has_collection();
    }

private:
    void set_has_collection() { _has_bits_[0] |= 0x2u; }
    void clear_has_collection() { _has_bits_[0] &= ~0x2u; }
  
    std::string collection_;

  // query
public:
    static const int kqueryFieldNumber = 3;
    const BsonPtr& query() const { return query_; }
    bool has_query() const { return _has_bits_[0] & 0x4u; }
    void clear_query() {
      clear_has_query();
      query_.reset();
    }
    void set_query(BsonPtr value) {
      query_ = value;
      set_has_query();
    }

private:
    void set_has_query() { _has_bits_[0] |= 0x4u; }
    void clear_has_query() { _has_bits_[0] &= ~0x4u; }
  
    BsonPtr query_;

  // sort
public:
    static const int ksortFieldNumber = 4;
    const BsonPtr& sort() const { return sort_; }
    bool has_sort() const { return _has_bits_[0] & 0x8u; }
    void clear_sort() {
      clear_has_sort();
      sort_.reset();
    }
    void set_sort(BsonPtr value) {
      sort_ = value;
      set_has_sort();
    }
  
private:
    void set_has_sort() { _has_bits_[0] |= 0x8u; }
    void clear_has_sort() { _has_bits_[0] &= ~0x8u; }

    BsonPtr sort_;

  // skip
public:
    static const int kskipFieldNumber = 5;
    int32_t skip() const { return skip_; }
    bool has_skip() const { return _has_bits_[0] & 0x10u; }
    void clear_skip() {
        clear_has_skip();
        skip_ = 0;
    }
    void set_skip(int32_t value) {
        skip_ = value;
        set_has_skip();
    }

private:
    void set_has_skip() { _has_bits_[0] |= 0x10u; }
    void clear_has_skip() { _has_bits_[0] &= ~0x10u; }
  
    int32_t skip_;

  // limit
public:
    static const int klimitFieldNumber = 6;
    int32_t limit() const { return limit_; }
    bool has_limit() const { return _has_bits_[0] & 0x20u; }
    void clear_limit() {
        clear_has_limit();
        limit_ = 0;
    }
    void set_limit(int32_t value) {
        limit_ = value;
        set_has_limit();
    }

private:
    void set_has_limit() { _has_bits_[0] |= 0x20u; }
    void clear_has_limit() { _has_bits_[0] &= ~0x20u; }
  
    int32_t limit_;

  // fields
public:
    static const int kfieldsFieldNumber = 7;
    const std::vector<std::string>& fields() const { return fields_; }
    int fields_size() const { return fields_.size(); }
    void clear_fields() { fields_.clear(); }
    const std::string& fields(int index) const { return fields_[index]; }
    std::string* mutable_fields(int index) { return &fields_[index]; }
    void add_fields(std::string value) { fields_.push_back(std::move(value)); }

private:
    std::vector<std::string> fields_;

private:
    void SharedCtor();
    void SharedDtor();
    void SetCachedSize(int size) const;
  
    ::google::protobuf::internal::HasBits<1> _has_bits_;
    mutable int _cached_size_;
};

class MongoQueryResponse : public MongoResponse {
 public:
  MongoQueryResponse();
  virtual ~MongoQueryResponse();
  MongoQueryResponse(const MongoQueryResponse& from);
  inline MongoQueryResponse& operator=(const MongoQueryResponse& from) {
    CopyFrom(from);
    return *this;
  }
  void Swap(MongoQueryResponse* other);

  int64_t cursorid() const;
  bool has_cursorid() const;
  void clear_cursorid();
  void set_cursorid(int64_t cursorid);
  static const int kCursoridFieldNumber = 1;

  int32_t starting_from() const;
  bool has_starting_from() const;
  void clear_starting_from();
  void set_starting_from(int32_t value);
  static const int kStartingFromFieldNumber = 2;

  int32_t number_returned() const;
  bool has_number_returned() const;
  void clear_number_returned();
  void set_number_returned(int32_t value);
  static const int kNumberReturnedFieldNumber = 3;

  int documents_size() const;
  void clear_documents();
  BsonPtr* mutable_documents(int index);
  std::vector<BsonPtr>* mutable_documents();
  const BsonPtr& documents(int index) const;
  void add_documents(const BsonPtr&);
  const std::vector<BsonPtr>& documents() const;
  static const int kDocumentsFieldNumber = 4;

  std::string ns() const;
  bool has_ns() const;
  void clear_ns();
  void set_ns(std::string value);
  static const int kNSfieldNumber = 5;

  MongoQueryResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoQueryResponse& from);
  void MergeFrom(const MongoQueryResponse& from);
  void Clear();
  bool IsInitialized() const;
  // int ByteSize() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }

  static const ::google::protobuf::Descriptor* descriptor();

  // void Print(std::ostream&) const;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  void set_has_cursorid();
  void clear_has_cursorid();

  void set_has_starting_from();
  void clear_has_starting_from();

  void set_has_number_returned();
  void clear_has_number_returned();

  void set_has_ns();
  void clear_has_ns();

  int64_t cursorid_;
  int32_t starting_from_;
  int32_t number_returned_;
  std::vector<BsonPtr> documents_;
  std::string ns_;
  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

inline int64_t MongoQueryResponse::cursorid() const { return cursorid_; }

inline bool MongoQueryResponse::has_cursorid() const {
  return _has_bits_[0] & 0x00000001u;
}

inline void MongoQueryResponse::clear_cursorid() {
  clear_has_cursorid();
  cursorid_ = 0;
}

inline void MongoQueryResponse::set_cursorid(int64_t cursorid) {
  cursorid_ = cursorid;
  set_has_cursorid();
}

inline void MongoQueryResponse::set_has_cursorid() {
  _has_bits_[0] |= 0x00000001u;
}

inline void MongoQueryResponse::clear_has_cursorid() {
  _has_bits_[0] &= ~0x00000001u;
}

inline int32_t MongoQueryResponse::starting_from() const {
  return starting_from_;
}

inline bool MongoQueryResponse::has_starting_from() const {
  return _has_bits_[0] & 0x00000002u;
}

inline void MongoQueryResponse::clear_starting_from() {
  clear_has_starting_from();
  starting_from_ = 0;
}

inline void MongoQueryResponse::set_starting_from(int32_t value) {
  starting_from_ = value;
  set_has_starting_from();
}

inline void MongoQueryResponse::set_has_starting_from() {
  _has_bits_[0] |= 0x00000002u;
}

inline void MongoQueryResponse::clear_has_starting_from() {
  _has_bits_[0] &= ~0x00000002u;
}

inline int32_t MongoQueryResponse::number_returned() const {
  return number_returned_;
}

inline bool MongoQueryResponse::has_number_returned() const {
  return _has_bits_[0] & 0x00000004u;
}

inline void MongoQueryResponse::clear_number_returned() {
  clear_has_number_returned();
  number_returned_ = 0;
}

inline void MongoQueryResponse::set_number_returned(int32_t value) {
  number_returned_ = value;
  set_has_number_returned();
}

inline void MongoQueryResponse::set_has_number_returned() {
  _has_bits_[0] |= 0x00000004u;
}

inline void MongoQueryResponse::clear_has_number_returned() {
  _has_bits_[0] &= ~0x00000004u;
}

inline int MongoQueryResponse::documents_size() const {
  return documents_.size();
}

inline void MongoQueryResponse::clear_documents() { documents_.clear(); }

inline BsonPtr* MongoQueryResponse::mutable_documents(int index) {
  return &documents_[index];
}

inline std::vector<BsonPtr>* MongoQueryResponse::mutable_documents() {
  return &documents_;
}

inline const BsonPtr& MongoQueryResponse::documents(int index) const {
  return documents_[index];
}

inline void MongoQueryResponse::add_documents(const BsonPtr& value) {
  documents_.push_back(value);
}

inline const std::vector<BsonPtr>& MongoQueryResponse::documents() const {
  return documents_;
}

inline std::string MongoQueryResponse::ns() const { return ns_; }

inline bool MongoQueryResponse::has_ns() const {
  return _has_bits_[0] & 0x00000010u;
}

inline void MongoQueryResponse::clear_ns() {
  clear_has_ns();
  ns_.clear();
}

inline void MongoQueryResponse::set_ns(std::string value) {
  ns_ = value;
  set_has_ns();
}

inline void MongoQueryResponse::set_has_ns() { _has_bits_[0] |= 0x00000010u; }

inline void MongoQueryResponse::clear_has_ns() {
  _has_bits_[0] &= ~0x00000010u;
}

inline void MongoQueryResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

class MongoGetMoreRequest : public MongoRequest {
 public:
  MongoGetMoreRequest();
  virtual ~MongoGetMoreRequest();
  MongoGetMoreRequest(const MongoGetMoreRequest& from);
  inline MongoGetMoreRequest& operator=(const MongoGetMoreRequest& from) {
    CopyFrom(from);
    return *this;
  }
  void Swap(MongoGetMoreRequest* other);

  // database
  const std::string& database() const;
  bool has_database() const;
  void clear_database();
  void set_database(std::string database);
  static const int kDatabaseFieldNumber = 1;

  // collection
  const std::string& collection() const;
  bool has_collection() const;
  void clear_collection();
  void set_collection(std::string collection);
  static const int kCollectionFieldNumber = 2;

  // cursor_id
  int64_t cursorid() const;
  bool has_cursorid() const;
  void clear_cursorid();
  void set_cursorid(int64_t cursorid);
  static const int kCursorIdFieldNumber = 3;

  // batch_size
  int32_t batch_size() const;
  bool has_batch_size() const;
  void clear_batch_size();
  void set_batch_size(int32_t batch_size);
  static const int kBatchSizeFieldNumber = 4;

  // maxTimeMS
  int32_t max_time_ms() const;
  bool has_max_time_ms() const;
  void clear_max_time_ms();
  void set_max_time_ms(int32_t max_time_ms);
  static const int kMaxTimeMSFieldNumber = 5;

  // comment
  BsonPtr comment() const;
  bool has_comment() const;
  void clear_comment();
  void set_comment(BsonPtr comment);
  static const int kCommentFieldNumber = 6;

  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
        return "query_getMore";
  }

  MongoGetMoreRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoGetMoreRequest& from);
  void MergeFrom(const MongoGetMoreRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }

  static const ::google::protobuf::Descriptor* descriptor();

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  void set_has_database();
  void clear_has_database();

  void set_has_collection();
  void clear_has_collection();

  void set_has_cursorid();
  void clear_has_cursorid();

  void set_has_batch_size();
  void clear_has_batch_size();

  void set_has_max_time_ms();
  void clear_has_max_time_ms();

  void set_has_comment();
  void clear_has_comment();

  std::string database_;
  std::string collection_;
  int64_t cursorid_;
  int32_t batch_size_;
  int32_t max_time_ms_;
  BsonPtr comment_;
  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

inline const std::string& MongoGetMoreRequest::database() const {
  return database_;
}

inline bool MongoGetMoreRequest::has_database() const {
  return _has_bits_[0] & 0x00000001u;
}

inline void MongoGetMoreRequest::clear_database() {
  clear_has_database();
  database_.clear();
}

inline void MongoGetMoreRequest::set_database(std::string database) {
  database_ = database;
  set_has_database();
}

inline void MongoGetMoreRequest::set_has_database() {
  _has_bits_[0] |= 0x00000001u;
}

inline void MongoGetMoreRequest::clear_has_database() {
  _has_bits_[0] &= ~0x00000001u;
}

inline const std::string& MongoGetMoreRequest::collection() const {
  return collection_;
}

inline bool MongoGetMoreRequest::has_collection() const {
  return _has_bits_[0] & 0x00000002u;
}

inline void MongoGetMoreRequest::clear_collection() {
  clear_has_collection();
  collection_.clear();
}
inline void MongoGetMoreRequest::set_collection(std::string collection) {
  collection_ = collection;
  set_has_collection();
}

inline void MongoGetMoreRequest::set_has_collection() {
  _has_bits_[0] |= 0x00000002u;
}

inline void MongoGetMoreRequest::clear_has_collection() {
  _has_bits_[0] &= ~0x00000002u;
}

inline int64_t MongoGetMoreRequest::cursorid() const { return cursorid_; }

inline bool MongoGetMoreRequest::has_cursorid() const {
  return _has_bits_[0] & 0x00000004u;
}

inline void MongoGetMoreRequest::clear_cursorid() {
  clear_has_cursorid();
  cursorid_ = 0;
}

inline void MongoGetMoreRequest::set_cursorid(int64_t cursorid) {
  cursorid_ = cursorid;
  set_has_cursorid();
}

inline void MongoGetMoreRequest::set_has_cursorid() {
  _has_bits_[0] |= 0x00000004u;
}

inline void MongoGetMoreRequest::clear_has_cursorid() {
  _has_bits_[0] &= ~0x00000004u;
}

inline int32_t MongoGetMoreRequest::batch_size() const { return batch_size_; }

inline bool MongoGetMoreRequest::has_batch_size() const {
  return _has_bits_[0] & 0x00000008u;
}

inline void MongoGetMoreRequest::clear_batch_size() {
  batch_size_ = 0;
  clear_has_batch_size();
}

inline void MongoGetMoreRequest::set_batch_size(int32_t batch_size) {
  batch_size_ = batch_size;
  set_has_batch_size();
}

inline void MongoGetMoreRequest::set_has_batch_size() {
  _has_bits_[0] |= 0x00000008u;
}

inline void MongoGetMoreRequest::clear_has_batch_size() {
  _has_bits_[0] &= ~0x00000008u;
}

inline int32_t MongoGetMoreRequest::max_time_ms() const { return max_time_ms_; }

inline bool MongoGetMoreRequest::has_max_time_ms() const {
  return _has_bits_[0] & 0x00000010u;
}

inline void MongoGetMoreRequest::clear_max_time_ms() {
  max_time_ms_ = 0;
  clear_has_max_time_ms();
}

inline void MongoGetMoreRequest::set_max_time_ms(int32_t max_time_ms) {
  max_time_ms_ = max_time_ms;
  set_has_max_time_ms();
}

inline void MongoGetMoreRequest::set_has_max_time_ms() {
  _has_bits_[0] |= 0x00000010u;
}

inline void MongoGetMoreRequest::clear_has_max_time_ms() {
  _has_bits_[0] &= ~0x00000010u;
}

inline BsonPtr MongoGetMoreRequest::comment() const { return comment_; }

inline bool MongoGetMoreRequest::has_comment() const {
  return _has_bits_[0] & 0x00000020u;
}

inline void MongoGetMoreRequest::clear_comment() {
  clear_has_comment();
  comment_.reset();
}

inline void MongoGetMoreRequest::set_comment(BsonPtr comment) {
  comment_ = comment;
  set_has_comment();
}

inline void MongoGetMoreRequest::set_has_comment() {
  _has_bits_[0] |= 0x00000020u;
}

inline void MongoGetMoreRequest::clear_has_comment() {
  _has_bits_[0] &= ~0x00000020u;
}

class MongoCountRequest : public MongoRequest {
 public:
  MongoCountRequest();
  virtual ~MongoCountRequest();
  MongoCountRequest(const MongoCountRequest& from);
  MongoCountRequest& operator=(const MongoCountRequest& from);
  void Swap(MongoCountRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "count";
  }
  MongoCountRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoCountRequest& from);
  void MergeFrom(const MongoCountRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields
  // database
 public:
  static const int kdatabaseFieldNumber = 1;
  const std::string& database() const { return database_; }
  bool has_database() const { return _has_bits_[0] & 0x1u; }
  void clear_database() {
    clear_has_database();
    database_.clear();
  }
  void set_database(std::string value) {
    database_ = value;
    set_has_database();
  }

 private:
  void set_has_database() { _has_bits_[0] |= 0x1u; }
  void clear_has_database() { _has_bits_[0] &= ~0x1u; }

  std::string database_;

  // collection
 public:
  static const int kcollectionFieldNumber = 2;
  const std::string& collection() const { return collection_; }
  bool has_collection() const { return _has_bits_[0] & 0x2u; }
  void clear_collection() {
    clear_has_collection();
    collection_.clear();
  }
  void set_collection(std::string value) {
    collection_ = value;
    set_has_collection();
  }

 private:
  void set_has_collection() { _has_bits_[0] |= 0x2u; }
  void clear_has_collection() { _has_bits_[0] &= ~0x2u; }

  std::string collection_;

  // query
 public:
  static const int kqueryFieldNumber = 3;
  const BsonPtr& query() const { return query_; }
  bool has_query() const { return _has_bits_[0] & 0x4u; }
  void clear_query() {
    clear_has_query();
    query_.reset();
  }
  void set_query(BsonPtr value) {
    query_ = value;
    set_has_query();
  }

 private:
  void set_has_query() { _has_bits_[0] |= 0x4u; }
  void clear_has_query() { _has_bits_[0] &= ~0x4u; }

  BsonPtr query_;

  // skip
 public:
  static const int kskipFieldNumber = 4;
  int64_t skip() const { return skip_; }
  bool has_skip() const { return _has_bits_[0] & 0x8u; }
  void clear_skip() {
    clear_has_skip();
    skip_ = 0;
  }
  void set_skip(int64_t value) {
    skip_ = value;
    set_has_skip();
  }

 private:
  void set_has_skip() { _has_bits_[0] |= 0x8u; }
  void clear_has_skip() { _has_bits_[0] &= ~0x8u; }

  int64_t skip_;

  // limit
 public:
  static const int klimitFieldNumber = 5;
  int64_t limit() const { return limit_; }
  bool has_limit() const { return _has_bits_[0] & 0x10u; }
  void clear_limit() {
    clear_has_limit();
    limit_ = 0;
  }
  void set_limit(int64_t value) {
    limit_ = value;
    set_has_limit();
  }

 private:
  void set_has_limit() { _has_bits_[0] |= 0x10u; }
  void clear_has_limit() { _has_bits_[0] &= ~0x10u; }

  int64_t limit_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoCountResponse : public MongoResponse {
 public:
  MongoCountResponse();
  virtual ~MongoCountResponse();
  MongoCountResponse(const MongoCountResponse& from);
  MongoCountResponse& operator=(const MongoCountResponse& from);
  void Swap(MongoCountResponse* other);
  MongoCountResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoCountResponse& from);
  void MergeFrom(const MongoCountResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields
  // number
 public:
  static const int knumberFieldNumber = 1;
  int32_t number() const { return number_; }
  bool has_number() const { return _has_bits_[0] & 0x1u; }
  void clear_number() {
    clear_has_number();
    number_ = 0;
  }
  void set_number(int32_t value) {
    number_ = value;
    set_has_number();
  }

 private:
  void set_has_number() { _has_bits_[0] |= 0x1u; }
  void clear_has_number() { _has_bits_[0] &= ~0x1u; }

  int32_t number_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoInsertRequest : public MongoRequest {
 public:
  MongoInsertRequest();
  virtual ~MongoInsertRequest();
  MongoInsertRequest(const MongoInsertRequest& from);
  MongoInsertRequest& operator=(const MongoInsertRequest& from);
  void Swap(MongoInsertRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "insert";
  }
  MongoInsertRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoInsertRequest& from);
  void MergeFrom(const MongoInsertRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // database
 public:
  static const int kdatabaseFieldNumber = 1;
  const std::string& database() const { return database_; }
  bool has_database() const { return _has_bits_[0] & 0x1u; }
  void clear_database() {
    clear_has_database();
    database_.clear();
  }
  void set_database(std::string value) {
    database_ = value;
    set_has_database();
  }

 private:
  void set_has_database() { _has_bits_[0] |= 0x1u; }
  void clear_has_database() { _has_bits_[0] &= ~0x1u; }

  std::string database_;

  // collection
 public:
  static const int kcollectionFieldNumber = 2;
  const std::string& collection() const { return collection_; }
  bool has_collection() const { return _has_bits_[0] & 0x2u; }
  void clear_collection() {
    clear_has_collection();
    collection_.clear();
  }
  void set_collection(std::string value) {
    collection_ = value;
    set_has_collection();
  }

 private:
  void set_has_collection() { _has_bits_[0] |= 0x2u; }
  void clear_has_collection() { _has_bits_[0] &= ~0x2u; }

  std::string collection_;

  // ordered
 public:
  static const int korderedFieldNumber = 3;
  bool ordered() const { return ordered_; }
  bool has_ordered() const { return _has_bits_[0] & 0x4u; }
  void clear_ordered() {
    clear_has_ordered();
    ordered_ = 0;
  }
  void set_ordered(bool value) {
    ordered_ = value;
    set_has_ordered();
  }

 private:
  void set_has_ordered() { _has_bits_[0] |= 0x4u; }
  void clear_has_ordered() { _has_bits_[0] &= ~0x4u; }

  bool ordered_;

  // documents
 public:
  static const int kdocumentsFieldNumber = 4;
  const std::vector<BsonPtr>& documents() const { return documents_; }
  int documents_size() const { return documents_.size(); }
  void clear_documents() { documents_.clear(); }
  const BsonPtr& documents(int index) const { return documents_[index]; }
  BsonPtr* mutable_documents(int index) { return &documents_[index]; }
  void add_documents(BsonPtr value) { documents_.push_back(std::move(value)); }

 private:
  std::vector<BsonPtr> documents_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

struct WriteError {
  int32_t index;
  int32_t code;
  std::string errmsg;
};

class MongoInsertResponse : public MongoResponse {
 public:
  MongoInsertResponse();
  virtual ~MongoInsertResponse();
  MongoInsertResponse(const MongoInsertResponse& from);
  MongoInsertResponse& operator=(const MongoInsertResponse& from);
  void Swap(MongoInsertResponse* other);
  MongoInsertResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoInsertResponse& from);
  void MergeFrom(const MongoInsertResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // number
 public:
  static const int knumberFieldNumber = 1;
  int32_t number() const { return number_; }
  bool has_number() const { return _has_bits_[0] & 0x1u; }
  void clear_number() {
    clear_has_number();
    number_ = 0;
  }
  void set_number(int32_t value) {
    number_ = value;
    set_has_number();
  }

 private:
  void set_has_number() { _has_bits_[0] |= 0x1u; }
  void clear_has_number() { _has_bits_[0] &= ~0x1u; }

  int32_t number_;

  // write_errors
 public:
  static const int kwrite_errorsFieldNumber = 2;
  const std::vector<WriteError>& write_errors() const { return write_errors_; }
  int write_errors_size() const { return write_errors_.size(); }
  void clear_write_errors() { write_errors_.clear(); }
  const WriteError& write_errors(int index) const {
    return write_errors_[index];
  }
  WriteError* mutable_write_errors(int index) { return &write_errors_[index]; }
  void add_write_errors(WriteError value) {
    write_errors_.push_back(std::move(value));
  }

 private:
  std::vector<WriteError> write_errors_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoDeleteRequest : public MongoRequest {
 public:
  MongoDeleteRequest();
  virtual ~MongoDeleteRequest();
  MongoDeleteRequest(const MongoDeleteRequest& from);
  MongoDeleteRequest& operator=(const MongoDeleteRequest& from);
  void Swap(MongoDeleteRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "delete";
  }
  MongoDeleteRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoDeleteRequest& from);
  void MergeFrom(const MongoDeleteRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // database
 public:
  static const int kdatabaseFieldNumber = 1;
  const std::string& database() const { return database_; }
  bool has_database() const { return _has_bits_[0] & 0x1u; }
  void clear_database() {
    clear_has_database();
    database_.clear();
  }
  void set_database(std::string value) {
    database_ = value;
    set_has_database();
  }

 private:
  void set_has_database() { _has_bits_[0] |= 0x1u; }
  void clear_has_database() { _has_bits_[0] &= ~0x1u; }

  std::string database_;

  // collection
 public:
  static const int kcollectionFieldNumber = 2;
  const std::string& collection() const { return collection_; }
  bool has_collection() const { return _has_bits_[0] & 0x2u; }
  void clear_collection() {
    clear_has_collection();
    collection_.clear();
  }
  void set_collection(std::string value) {
    collection_ = value;
    set_has_collection();
  }

 private:
  void set_has_collection() { _has_bits_[0] |= 0x2u; }
  void clear_has_collection() { _has_bits_[0] &= ~0x2u; }

  std::string collection_;

  // ordered
 public:
  static const int korderedFieldNumber = 3;
  bool ordered() const { return ordered_; }
  bool has_ordered() const { return _has_bits_[0] & 0x4u; }
  void clear_ordered() {
    clear_has_ordered();
    ordered_ = false;
  }
  void set_ordered(bool value) {
    ordered_ = value;
    set_has_ordered();
  }

 private:
  void set_has_ordered() { _has_bits_[0] |= 0x4u; }
  void clear_has_ordered() { _has_bits_[0] &= ~0x4u; }

  bool ordered_;

  // query
 public:
  static const int kqueryFieldNumber = 4;
  const BsonPtr& query() const { return query_; }
  bool has_query() const { return _has_bits_[0] & 0x8u; }
  void clear_query() {
    clear_has_query();
    query_.reset();
  }
  void set_query(BsonPtr value) {
    query_ = value;
    set_has_query();
  }

 private:
  void set_has_query() { _has_bits_[0] |= 0x8u; }
  void clear_has_query() { _has_bits_[0] &= ~0x8u; }

  BsonPtr query_;

  // delete_many
 public:
  static const int kdelete_manyFieldNumber = 5;
  bool delete_many() const { return delete_many_; }
  bool has_delete_many() const { return _has_bits_[0] & 0x10u; }
  void clear_delete_many() {
    clear_has_delete_many();
    delete_many_ = false;
  }
  void set_delete_many(bool value) {
    delete_many_ = value;
    set_has_delete_many();
  }

 private:
  void set_has_delete_many() { _has_bits_[0] |= 0x10u; }
  void clear_has_delete_many() { _has_bits_[0] &= ~0x10u; }

  bool delete_many_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoDeleteResponse : public MongoResponse {
 public:
  MongoDeleteResponse();
  virtual ~MongoDeleteResponse();
  MongoDeleteResponse(const MongoDeleteResponse& from);
  MongoDeleteResponse& operator=(const MongoDeleteResponse& from);
  void Swap(MongoDeleteResponse* other);
  MongoDeleteResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoDeleteResponse& from);
  void MergeFrom(const MongoDeleteResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // number
 public:
  static const int knumberFieldNumber = 1;
  int32_t number() const { return number_; }
  bool has_number() const { return _has_bits_[0] & 0x1u; }
  void clear_number() {
    clear_has_number();
    number_ = 0;
  }
  void set_number(int32_t value) {
    number_ = value;
    set_has_number();
  }

 private:
  void set_has_number() { _has_bits_[0] |= 0x1u; }
  void clear_has_number() { _has_bits_[0] &= ~0x1u; }

  int32_t number_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoUpdateRequest : public MongoRequest {
 public:
  MongoUpdateRequest();
  virtual ~MongoUpdateRequest();
  MongoUpdateRequest(const MongoUpdateRequest& from);
  MongoUpdateRequest& operator=(const MongoUpdateRequest& from);
  void Swap(MongoUpdateRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "update";
  }
  MongoUpdateRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoUpdateRequest& from);
  void MergeFrom(const MongoUpdateRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // database
 public:
  static const int kdatabaseFieldNumber = 1;
  const std::string& database() const { return database_; }
  bool has_database() const { return _has_bits_[0] & 0x1u; }
  void clear_database() {
    clear_has_database();
    database_.clear();
  }
  void set_database(std::string value) {
    database_ = value;
    set_has_database();
  }

 private:
  void set_has_database() { _has_bits_[0] |= 0x1u; }
  void clear_has_database() { _has_bits_[0] &= ~0x1u; }

  std::string database_;

  // collection
 public:
  static const int kcollectionFieldNumber = 2;
  const std::string& collection() const { return collection_; }
  bool has_collection() const { return _has_bits_[0] & 0x2u; }
  void clear_collection() {
    clear_has_collection();
    collection_.clear();
  }
  void set_collection(std::string value) {
    collection_ = value;
    set_has_collection();
  }

 private:
  void set_has_collection() { _has_bits_[0] |= 0x2u; }
  void clear_has_collection() { _has_bits_[0] &= ~0x2u; }

  std::string collection_;

  // selector
 public:
  static const int kselectorFieldNumber = 3;
  const BsonPtr& selector() const { return selector_; }
  bool has_selector() const { return _has_bits_[0] & 0x4u; }
  void clear_selector() {
    clear_has_selector();
    selector_.reset();
  }
  void set_selector(BsonPtr value) {
    selector_ = value;
    set_has_selector();
  }

 private:
  void set_has_selector() { _has_bits_[0] |= 0x4u; }
  void clear_has_selector() { _has_bits_[0] &= ~0x4u; }

  BsonPtr selector_;

  // update
 public:
  static const int kupdateFieldNumber = 4;
  const BsonPtr& update() const { return update_; }
  bool has_update() const { return _has_bits_[0] & 0x8u; }
  void clear_update() {
    clear_has_update();
    update_.reset();
  }
  void set_update(BsonPtr value) {
    update_ = value;
    set_has_update();
  }

 private:
  void set_has_update() { _has_bits_[0] |= 0x8u; }
  void clear_has_update() { _has_bits_[0] &= ~0x8u; }

  BsonPtr update_;

  // ordered
 public:
  static const int korderedFieldNumber = 5;
  bool ordered() const { return ordered_; }
  bool has_ordered() const { return _has_bits_[0] & 0x10u; }
  void clear_ordered() {
    clear_has_ordered();
    ordered_ = false;
  }
  void set_ordered(bool value) {
    ordered_ = value;
    set_has_ordered();
  }

 private:
  void set_has_ordered() { _has_bits_[0] |= 0x10u; }
  void clear_has_ordered() { _has_bits_[0] &= ~0x10u; }

  bool ordered_;

  // upsert
 public:
  static const int kupsertFieldNumber = 6;
  bool upsert() const { return upsert_; }
  bool has_upsert() const { return _has_bits_[0] & 0x20u; }
  void clear_upsert() {
    clear_has_upsert();
    upsert_ = false;
  }
  void set_upsert(bool value) {
    upsert_ = value;
    set_has_upsert();
  }

 private:
  void set_has_upsert() { _has_bits_[0] |= 0x20u; }
  void clear_has_upsert() { _has_bits_[0] &= ~0x20u; }

  bool upsert_;

  // multi
 public:
  static const int kmultiFieldNumber = 7;
  bool multi() const { return multi_; }
  bool has_multi() const { return _has_bits_[0] & 0x40u; }
  void clear_multi() {
    clear_has_multi();
    multi_ = false;
  }
  void set_multi(bool value) {
    multi_ = value;
    set_has_multi();
  }

 private:
  void set_has_multi() { _has_bits_[0] |= 0x40u; }
  void clear_has_multi() { _has_bits_[0] &= ~0x40u; }

  bool multi_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

struct UpsertedDoc {
  int32_t index;
  bson_oid_t _id;
};

class MongoUpdateResponse : public MongoResponse {
 public:
  MongoUpdateResponse();
  virtual ~MongoUpdateResponse();
  MongoUpdateResponse(const MongoUpdateResponse& from);
  MongoUpdateResponse& operator=(const MongoUpdateResponse& from);
  void Swap(MongoUpdateResponse* other);
  MongoUpdateResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoUpdateResponse& from);
  void MergeFrom(const MongoUpdateResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // matched_number
 public:
  static const int kmatched_numberFieldNumber = 1;
  int32_t matched_number() const { return matched_number_; }
  bool has_matched_number() const { return _has_bits_[0] & 0x1u; }
  void clear_matched_number() {
    clear_has_matched_number();
    matched_number_ = 0;
  }
  void set_matched_number(int32_t value) {
    matched_number_ = value;
    set_has_matched_number();
  }

 private:
  void set_has_matched_number() { _has_bits_[0] |= 0x1u; }
  void clear_has_matched_number() { _has_bits_[0] &= ~0x1u; }

  int32_t matched_number_;

  // modified_number
 public:
  static const int kmodified_numberFieldNumber = 2;
  int32_t modified_number() const { return modified_number_; }
  bool has_modified_number() const { return _has_bits_[0] & 0x2u; }
  void clear_modified_number() {
    clear_has_modified_number();
    modified_number_ = 0;
  }
  void set_modified_number(int32_t value) {
    modified_number_ = value;
    set_has_modified_number();
  }

 private:
  void set_has_modified_number() { _has_bits_[0] |= 0x2u; }
  void clear_has_modified_number() { _has_bits_[0] &= ~0x2u; }

  int32_t modified_number_;

  // upserted_docs
 public:
  static const int kupserted_docsFieldNumber = 3;
  const std::vector<UpsertedDoc>& upserted_docs() const {
    return upserted_docs_;
  }
  int upserted_docs_size() const { return upserted_docs_.size(); }
  void clear_upserted_docs() { upserted_docs_.clear(); }
  const UpsertedDoc& upserted_docs(int index) const {
    return upserted_docs_[index];
  }
  UpsertedDoc* mutable_upserted_docs(int index) {
    return &upserted_docs_[index];
  }
  void add_upserted_docs(UpsertedDoc value) {
    upserted_docs_.push_back(std::move(value));
  }

 private:
  std::vector<UpsertedDoc> upserted_docs_;

  // write_errors
 public:
  static const int kwrite_errorsFieldNumber = 4;
  const std::vector<WriteError>& write_errors() const { return write_errors_; }
  int write_errors_size() const { return write_errors_.size(); }
  void clear_write_errors() { write_errors_.clear(); }
  const WriteError& write_errors(int index) const {
    return write_errors_[index];
  }
  WriteError* mutable_write_errors(int index) { return &write_errors_[index]; }
  void add_write_errors(WriteError value) {
    write_errors_.push_back(std::move(value));
  }

 private:
  std::vector<WriteError> write_errors_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoFindAndModifyRequest : public MongoRequest {
 public:
  MongoFindAndModifyRequest();
  virtual ~MongoFindAndModifyRequest();
  MongoFindAndModifyRequest(const MongoFindAndModifyRequest& from);
  MongoFindAndModifyRequest& operator=(const MongoFindAndModifyRequest& from);
  void Swap(MongoFindAndModifyRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "find_and_modify";
  }
  MongoFindAndModifyRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoFindAndModifyRequest& from);
  void MergeFrom(const MongoFindAndModifyRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // database
 public:
  static const int kdatabaseFieldNumber = 1;
  const std::string& database() const { return database_; }
  bool has_database() const { return _has_bits_[0] & 0x1u; }
  void clear_database() {
    clear_has_database();
    database_.clear();
  }
  void set_database(std::string value) {
    database_ = value;
    set_has_database();
  }

 private:
  void set_has_database() { _has_bits_[0] |= 0x1u; }
  void clear_has_database() { _has_bits_[0] &= ~0x1u; }

  std::string database_;

  // collection
 public:
  static const int kcollectionFieldNumber = 2;
  const std::string& collection() const { return collection_; }
  bool has_collection() const { return _has_bits_[0] & 0x2u; }
  void clear_collection() {
    clear_has_collection();
    collection_.clear();
  }
  void set_collection(std::string value) {
    collection_ = value;
    set_has_collection();
  }

 private:
  void set_has_collection() { _has_bits_[0] |= 0x2u; }
  void clear_has_collection() { _has_bits_[0] &= ~0x2u; }

  std::string collection_;

  // query
 public:
  static const int kqueryFieldNumber = 3;
  const BsonPtr& query() const { return query_; }
  bool has_query() const { return _has_bits_[0] & 0x4u; }
  void clear_query() {
    clear_has_query();
    query_.reset();
  }
  void set_query(BsonPtr value) {
    query_ = value;
    set_has_query();
  }

 private:
  void set_has_query() { _has_bits_[0] |= 0x4u; }
  void clear_has_query() { _has_bits_[0] &= ~0x4u; }

  BsonPtr query_;

  // sort
 public:
  static const int ksortFieldNumber = 4;
  const BsonPtr& sort() const { return sort_; }
  bool has_sort() const { return _has_bits_[0] & 0x8u; }
  void clear_sort() {
    clear_has_sort();
    sort_.reset();
  }
  void set_sort(BsonPtr value) {
    sort_ = value;
    set_has_sort();
  }

 private:
  void set_has_sort() { _has_bits_[0] |= 0x8u; }
  void clear_has_sort() { _has_bits_[0] &= ~0x8u; }

  BsonPtr sort_;

  // update
 public:
  static const int kupdateFieldNumber = 5;
  const BsonPtr& update() const { return update_; }
  bool has_update() const { return _has_bits_[0] & 0x10u; }
  void clear_update() {
    clear_has_update();
    update_.reset();
  }
  void set_update(BsonPtr value) {
    update_ = value;
    set_has_update();
  }

 private:
  void set_has_update() { _has_bits_[0] |= 0x10u; }
  void clear_has_update() { _has_bits_[0] &= ~0x10u; }

  BsonPtr update_;

  // upsert
 public:
  static const int kupsertFieldNumber = 6;
  bool upsert() const { return upsert_; }
  bool has_upsert() const { return _has_bits_[0] & 0x20u; }
  void clear_upsert() {
    clear_has_upsert();
    upsert_ = false;
  }
  void set_upsert(bool value) {
    upsert_ = value;
    set_has_upsert();
  }

 private:
  void set_has_upsert() { _has_bits_[0] |= 0x20u; }
  void clear_has_upsert() { _has_bits_[0] &= ~0x20u; }

  bool upsert_;

  // remove
 public:
  static const int kremoveFieldNumber = 7;
  bool remove() const { return remove_; }
  bool has_remove() const { return _has_bits_[0] & 0x40u; }
  void clear_remove() {
    clear_has_remove();
    remove_ = false;
  }
  void set_remove(bool value) {
    remove_ = value;
    set_has_remove();
  }

 private:
  void set_has_remove() { _has_bits_[0] |= 0x40u; }
  void clear_has_remove() { _has_bits_[0] &= ~0x40u; }

  bool remove_;

  // return_new
 public:
  static const int kreturn_newFieldNumber = 8;
  bool return_new() const { return return_new_; }
  bool has_return_new() const { return _has_bits_[0] & 0x80u; }
  void clear_return_new() {
    clear_has_return_new();
    return_new_ = false;
  }
  void set_return_new(bool value) {
    return_new_ = value;
    set_has_return_new();
  }

 private:
  void set_has_return_new() { _has_bits_[0] |= 0x80u; }
  void clear_has_return_new() { _has_bits_[0] &= ~0x80u; }

  bool return_new_;

  // fields
 public:
  static const int kfieldsFieldNumber = 9;
  const std::vector<std::string>& fields() const { return fields_; }
  int fields_size() const { return fields_.size(); }
  void clear_fields() { fields_.clear(); }
  const std::string& fields(int index) const { return fields_[index]; }
  std::string* mutable_fields(int index) { return &fields_[index]; }
  void add_fields(std::string value) { fields_.push_back(std::move(value)); }

 private:
  std::vector<std::string> fields_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoFindAndModifyResponse : public MongoResponse {
 public:
  MongoFindAndModifyResponse();
  virtual ~MongoFindAndModifyResponse();
  MongoFindAndModifyResponse(const MongoFindAndModifyResponse& from);
  MongoFindAndModifyResponse& operator=(const MongoFindAndModifyResponse& from);
  void Swap(MongoFindAndModifyResponse* other);
  MongoFindAndModifyResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoFindAndModifyResponse& from);
  void MergeFrom(const MongoFindAndModifyResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // value
 public:
  static const int kvalueFieldNumber = 1;
  const BsonPtr& value() const { return value_; }
  bool has_value() const { return _has_bits_[0] & 0x1u; }
  void clear_value() {
    clear_has_value();
    value_.reset();
  }
  void set_value(BsonPtr value) {
    value_ = value;
    set_has_value();
  }

 private:
  void set_has_value() { _has_bits_[0] |= 0x1u; }
  void clear_has_value() { _has_bits_[0] &= ~0x1u; }

  BsonPtr value_;

  // upserted
 public:
  static const int kupsertedFieldNumber = 2;
  const bson_oid_t& upserted() const { return upserted_; }
  bool has_upserted() const { return _has_bits_[0] & 0x2u; }
  void clear_upserted() {
    clear_has_upserted();
    memset(&upserted_, 0, sizeof(upserted_));
  }
  void set_upserted(bson_oid_t value) {
    upserted_ = value;
    set_has_upserted();
  }

 private:
  void set_has_upserted() { _has_bits_[0] |= 0x2u; }
  void clear_has_upserted() { _has_bits_[0] &= ~0x2u; }

  bson_oid_t upserted_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoGetReplSetStatusRequest : public MongoRequest {
 public:
  MongoGetReplSetStatusRequest();
  virtual ~MongoGetReplSetStatusRequest();
  MongoGetReplSetStatusRequest(const MongoGetReplSetStatusRequest& from);
  MongoGetReplSetStatusRequest& operator=(
      const MongoGetReplSetStatusRequest& from);
  void Swap(MongoGetReplSetStatusRequest* other);
  bool SerializeTo(butil::IOBuf* buf) const;
  const char* RequestId() const override {
    return "get_repl_set_status";
  }
  MongoGetReplSetStatusRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoGetReplSetStatusRequest& from);
  void MergeFrom(const MongoGetReplSetStatusRequest& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

class MongoGetReplSetStatusResponse : public MongoResponse {
 public:
  MongoGetReplSetStatusResponse();
  virtual ~MongoGetReplSetStatusResponse();
  MongoGetReplSetStatusResponse(const MongoGetReplSetStatusResponse& from);
  MongoGetReplSetStatusResponse& operator=(
      const MongoGetReplSetStatusResponse& from);
  void Swap(MongoGetReplSetStatusResponse* other);
  MongoGetReplSetStatusResponse* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoGetReplSetStatusResponse& from);
  void MergeFrom(const MongoGetReplSetStatusResponse& from);
  void Clear();
  bool IsInitialized() const;
  bool MergePartialFromCodedStream(
      ::google::protobuf::io::CodedInputStream* input);
  void SerializeWithCachedSizes(
      ::google::protobuf::io::CodedOutputStream* output) const;
  ::google::protobuf::uint8* SerializeWithCachedSizesToArray(
      ::google::protobuf::uint8* output) const;
  int GetCachedSize() const { return _cached_size_; }
  static const ::google::protobuf::Descriptor* descriptor();

  // fields

  // ok
 public:
  static const int kokFieldNumber = 1;
  bool ok() const { return ok_; }
  bool has_ok() const { return _has_bits_[0] & 0x1u; }
  void clear_ok() {
    clear_has_ok();
    ok_ = false;
  }
  void set_ok(bool value) {
    ok_ = value;
    set_has_ok();
  }

 private:
  void set_has_ok() { _has_bits_[0] |= 0x1u; }
  void clear_has_ok() { _has_bits_[0] &= ~0x1u; }

  bool ok_;

  // set
 public:
  static const int ksetFieldNumber = 2;
  const std::string& set() const { return set_; }
  bool has_set() const { return _has_bits_[0] & 0x2u; }
  void clear_set() {
    clear_has_set();
    set_.clear();
  }
  void set_set(std::string value) {
    set_ = value;
    set_has_set();
  }

 private:
  void set_has_set() { _has_bits_[0] |= 0x2u; }
  void clear_has_set() { _has_bits_[0] &= ~0x2u; }

  std::string set_;

  // myState
 public:
  static const int kmyStateFieldNumber = 3;
  int32_t myState() const { return myState_; }
  bool has_myState() const { return _has_bits_[0] & 0x4u; }
  void clear_myState() {
    clear_has_myState();
    myState_ = 0;
  }
  void set_myState(int32_t value) {
    myState_ = value;
    set_has_myState();
  }

 private:
  void set_has_myState() { _has_bits_[0] |= 0x4u; }
  void clear_has_myState() { _has_bits_[0] &= ~0x4u; }

  int32_t myState_;

  // members
 public:
  static const int kmembersFieldNumber = 4;
  const std::vector<ReplicaSetMember>& members() const { return members_; }
  int members_size() const { return members_.size(); }
  void clear_members() { members_.clear(); }
  const ReplicaSetMember& members(int index) const { return members_[index]; }
  ReplicaSetMember* mutable_members(int index) { return &members_[index]; }
  void add_members(ReplicaSetMember value) {
    members_.push_back(std::move(value));
  }

 private:
  std::vector<ReplicaSetMember> members_;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

}  // namespace brpc

#endif  // BRPC_MONGO_H
