#include "brpc/mongo.h"

#include <google/protobuf/reflection_ops.h>  // ReflectionOps::Merge

namespace brpc {

MongoQueryRequest::MongoQueryRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoQueryRequest::MongoQueryRequest(const MongoQueryRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

void MongoQueryRequest::SharedCtor() {
  skip_ = 0;
  limit_ = 0;
  _cached_size_ = 0;
}

MongoQueryRequest::~MongoQueryRequest() { SharedDtor(); }

void MongoQueryRequest::SharedDtor() {}

bool MongoQueryRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoQueryRequest not initialize";
    return false;
  }
  bson_t* query_element = bson_new();
  // collection
  BSON_APPEND_UTF8(query_element, "find", collection().c_str());
  // query_filter
  auto query_filter = query();
  if (!query_filter) {
    query_filter.reset(bson_new(), bson_free);
  }
  BSON_APPEND_DOCUMENT(query_element, "filter", query_filter.get());
  if (!fields().empty()) {
    // 是否需要bson_free
    bson_t* field_doc = bson_new();
    for (auto& field : fields()) {
      BSON_APPEND_INT32(field_doc, field.c_str(), 1);
    }
    BSON_APPEND_DOCUMENT(query_element, "projection", field_doc);
  }
  if (has_skip()) {
    BSON_APPEND_INT64(query_element, "skip", skip());
  }
  if (has_limit()) {
    BSON_APPEND_INT64(query_element, "limit", limit());
  }
  // database
  BSON_APPEND_UTF8(query_element, "$db", database().c_str());
  // Message Flags 4bytes
  // Section[]  Kind(1byte): Body(0); BodyDucument(Bson)
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);
  uint8_t kind = 0;  // Body kind
  buf->append(static_cast<void*>(&kind), 1);
  buf->append(static_cast<const void*>(bson_get_data(query_element)),
              query_element->len);
  return true;
}

MongoQueryRequest* MongoQueryRequest::New() const {
  return new MongoQueryRequest;
}

void MongoQueryRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoQueryRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoQueryRequest* source =
      dynamic_cast<const MongoQueryRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoQueryRequest::CopyFrom(const MongoQueryRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoQueryRequest::MergeFrom(const MongoQueryRequest& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from.has_database()) {
    set_database(from.database());
  }
  if (from.has_collection()) {
    set_collection(from.collection());
  }
  if (from.has_skip()) {
    set_skip(from.skip());
  }
  if (from.has_limit()) {
    set_limit(from.limit());
  }
  if (from.has_query()) {
    set_query(from.query_);
  }

  fields_.insert(fields_.end(), from.fields().cbegin(), from.fields().cend());
}

void MongoQueryRequest::Clear() {
  clear_database();
  clear_collection();
  clear_skip();
  clear_limit();
  clear_query();
  clear_fields();
}

bool MongoQueryRequest::IsInitialized() const {
  return has_database() && has_collection();
}

// int MongoQueryRequest::ByteSize() const {

// }

bool MongoQueryRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoQueryRequest";
  return true;
}
void MongoQueryRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoQueryRequest";
}
::google::protobuf::uint8* MongoQueryRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoQueryRequest::descriptor() {
  return MongoQueryRequestBase::descriptor();
}

// void MongoQueryRequest::Print(std::ostream&) const;

::google::protobuf::Metadata MongoQueryRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

MongoQueryResponse::MongoQueryResponse() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoQueryResponse::MongoQueryResponse(const MongoQueryResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

void MongoQueryResponse::SharedCtor() {
  cursorid_ = 0;
  starting_from_ = 0;
  number_returned_ = 0;
  _cached_size_ = 0;
}

MongoQueryResponse::~MongoQueryResponse() { SharedDtor(); }

void MongoQueryResponse::SharedDtor() {}

void MongoQueryResponse::Swap(MongoQueryResponse* other) {}

MongoQueryResponse* MongoQueryResponse::New() const {
  return new MongoQueryResponse;
}

void MongoQueryResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoQueryResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoQueryResponse* source =
      dynamic_cast<const MongoQueryResponse*>(&from);
  if (source == nullptr) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoQueryResponse::CopyFrom(const MongoQueryResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoQueryResponse::MergeFrom(const MongoQueryResponse& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from.has_cursorid()) {
    set_cursorid(from.cursorid());
  }
  if (from.has_starting_from()) {
    set_starting_from(from.starting_from());
  }
  if (from.has_number_returned()) {
    set_number_returned(from.number_returned());
  }
  documents_.insert(documents_.end(), from.documents_.cbegin(),
                    from.documents_.cend());
  if (from.has_ns()) {
    set_ns(from.ns());
  }
}

void MongoQueryResponse::Clear() {
  clear_cursorid();
  clear_starting_from();
  clear_number_returned();
  clear_documents();
  clear_ns();
}

bool MongoQueryResponse::IsInitialized() const { return true; }

// int ByteSize() const;
bool MongoQueryResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoQueryResponse";
  return true;
}
void MongoQueryResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoQueryResponse";
}
::google::protobuf::uint8* MongoQueryResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoQueryResponse::descriptor() {
  return MongoQueryResponseBase::descriptor();
}

::google::protobuf::Metadata MongoQueryResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

}  // namespace brpc
