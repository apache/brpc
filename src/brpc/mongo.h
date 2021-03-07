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

#include <string>

#include "brpc/mongo_head.h"
#include "brpc/policy/mongo.pb.h"

#include <google/protobuf/message.h>

#include <list>
#include <memory>
#include <unordered_map>

#include "brpc/callback.h"
#include "brpc/parse_result.h"
#include "brpc/proto_base.pb.h"
#include "brpc/socket.h"
#include "butil/arena.h"
#include "butil/iobuf.h"
#include "butil/strings/string_piece.h"

namespace brpc {

typedef std::shared_ptr<bson_t> BsonPtr;

class MongoQueryRequest : public ::google::protobuf::Message {
 public:
  MongoQueryRequest();
  virtual ~MongoQueryRequest();
  MongoQueryRequest(const MongoQueryRequest& from);
  inline MongoQueryRequest& operator=(const MongoQueryRequest& from) {
    CopyFrom(from);
    return *this;
  }
  void Swap(MongoQueryRequest* other);

  const std::string& database() const;
  bool has_database() const;
  void clear_database();
  void set_database(std::string database);
  static const int kDatabaseFieldNumber = 1;

  const std::string& collection() const;
  bool has_collection() const;
  void clear_collection();
  void set_collection(std::string collection);
  static const int kCollectionFieldNumber = 2;

  int32_t skip() const;
  bool has_skip() const;
  void clear_skip();
  void set_skip(int32_t skip);
  static const int kSkipFieldNumber = 3;

  int32_t limit() const;
  bool has_limit() const;
  void clear_limit();
  void set_limit(int32_t limit);
  static const int kLimitFieldNumber = 4;

  const BsonPtr query() const;
  bool has_query() const;
  void clear_query();
  void set_query(BsonPtr value);
  static const int kQueryFieldNumber = 5;

  const std::vector<std::string>& fields() const;
  int fields_size() const;
  void clear_fields();
  const std::string& fields(int index) const;
  std::string* mutable_fields(int index);
  void add_fields(std::string value);
  static const int kFieldsFieldNumber = 6;

  bool SerializeTo(butil::IOBuf* buf) const;

  MongoQueryRequest* New() const;
  void CopyFrom(const ::google::protobuf::Message& from);
  void MergeFrom(const ::google::protobuf::Message& from);
  void CopyFrom(const MongoQueryRequest& from);
  void MergeFrom(const MongoQueryRequest& from);
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

 protected:
  ::google::protobuf::Metadata GetMetadata() const override;

 private:
  void SharedCtor();
  void SharedDtor();
  void SetCachedSize(int size) const;

  void set_has_database();
  void clear_has_database();

  void set_has_collection();
  void clear_has_collection();

  void set_has_skip();
  void clear_has_skip();

  void set_has_query();
  void clear_has_query();

  void set_has_limit();
  void clear_has_limit();

  std::string database_;
  std::string collection_;
  int32_t skip_;
  int32_t limit_;
  BsonPtr query_;
  std::vector<std::string> fields_;
  ::google::protobuf::internal::HasBits<1> _has_bits_;
  mutable int _cached_size_;
};

inline const std::string& MongoQueryRequest::database() const {
  return database_;
}

inline void MongoQueryRequest::set_database(std::string database) {
  set_has_database();
  database_ = database;
}

inline bool MongoQueryRequest::has_database() const {
  return (_has_bits_[0] & 0x00000001u);
}

inline void MongoQueryRequest::clear_database() {
  database_.clear();
  clear_has_database();
}

inline void MongoQueryRequest::set_has_database() {
  _has_bits_[0] |= 0x00000001u;
}

inline void MongoQueryRequest::clear_has_database() {
  _has_bits_[0] &= ~0x00000001u;
}

inline const std::string& MongoQueryRequest::collection() const {
  return collection_;
}

inline void MongoQueryRequest::set_collection(std::string collection) {
  set_has_collection();
  collection_ = collection;
}

inline bool MongoQueryRequest::has_collection() const {
  return _has_bits_[0] & 0x00000002u;
}

inline void MongoQueryRequest::clear_collection() {
  collection_.clear();
  clear_has_collection();
}

inline void MongoQueryRequest::set_has_collection() {
  _has_bits_[0] |= 0x00000002u;
}

inline void MongoQueryRequest::clear_has_collection() {
  _has_bits_[0] &= ~0x00000002u;
}

inline int32_t MongoQueryRequest::skip() const { return skip_; }

inline bool MongoQueryRequest::has_skip() const {
  return _has_bits_[0] & 0x00000004u;
}

inline void MongoQueryRequest::clear_skip() {
  clear_has_skip();
  skip_ = 0;
}

inline void MongoQueryRequest::set_skip(int32_t skip) {
  skip_ = skip;
  set_has_skip();
}

inline void MongoQueryRequest::set_has_skip() { _has_bits_[0] |= 0x00000004u; }

inline void MongoQueryRequest::clear_has_skip() {
  _has_bits_[0] &= ~0x00000004u;
}

inline int32_t MongoQueryRequest::limit() const { return limit_; }

inline bool MongoQueryRequest::has_limit() const {
  return _has_bits_[0] & 0x00000008u;
}

inline void MongoQueryRequest::clear_limit() {
  clear_has_limit();
  limit_ = 0;
}

inline void MongoQueryRequest::set_limit(int32_t limit) {
  limit_ = limit;
  set_has_limit();
}

inline void MongoQueryRequest::set_has_limit() { _has_bits_[0] |= 0x00000008u; }

inline void MongoQueryRequest::clear_has_limit() {
  _has_bits_[0] &= ~0x00000008u;
}

inline const BsonPtr MongoQueryRequest::query() const { return query_; }

inline bool MongoQueryRequest::has_query() const {
  return _has_bits_[0] & 0x00000010u;
}

inline void MongoQueryRequest::clear_query() {
  clear_has_query();
  query_.reset();
  query_ = nullptr;
}

inline void MongoQueryRequest::set_query(BsonPtr value) {
  query_ = value;
  set_has_query();
}

inline void MongoQueryRequest::set_has_query() { _has_bits_[0] |= 0x00000010u; }

inline void MongoQueryRequest::clear_has_query() {
  _has_bits_[0] &= ~0x00000010u;
}

inline const std::vector<std::string>& MongoQueryRequest::fields() const {
  return fields_;
}

inline int MongoQueryRequest::fields_size() const { return fields_.size(); }

inline void MongoQueryRequest::clear_fields() { fields_.clear(); }

inline const std::string& MongoQueryRequest::fields(int index) const {
  // TODO(zhangke) 判断是否越界
  return fields_[index];
}

inline std::string* MongoQueryRequest::mutable_fields(int index) {
  // TODO(zhangke) 判断是否越界
  return &fields_[index];
}

inline void MongoQueryRequest::add_fields(std::string value) {
  fields_.push_back(std::move(value));
}

inline void MongoQueryRequest::SetCachedSize(int size) const {
  _cached_size_ = size;
}

class MongoQueryResponse : public ::google::protobuf::Message {
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

  bool SerializeTo(butil::IOBuf* buf) const;

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

 protected:
  ::google::protobuf::Metadata GetMetadata() const override;

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

inline std::string MongoQueryResponse::ns() const {
  return ns_;
}

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

inline void MongoQueryResponse::set_has_ns() {
  _has_bits_[0] |= 0x00000010u;
}

inline void MongoQueryResponse::clear_has_ns() {
  _has_bits_[0] &= ~0x00000010u;
}

inline void MongoQueryResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

}  // namespace brpc

#endif  // BRPC_MONGO_H
