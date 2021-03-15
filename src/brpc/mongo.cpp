#include "brpc/mongo.h"

#include <google/protobuf/reflection_ops.h>  // ReflectionOps::Merge

#include "butil/bson_util.h"

namespace brpc {

bool DocumentSequence::SerializeTo(butil::IOBuf* buf) const {
  if (identifier.empty()) {
    return false;
  }
  // 计算size
  int32_t total_size = 4;  // int32_t size
  total_size += (identifier.size() + 1);
  for (auto& document : documents) {
    if (!document) {
      return false;
    }
    total_size += document.get()->len;
  }
  size = total_size;
  buf->append(static_cast<void*>(&size), 4);
  buf->append(identifier);
  buf->push_back(0);
  for (auto& document : documents) {
    buf->append(static_cast<const void*>(bson_get_data(document.get())),
                document.get()->len);
  }
  assert(buf->length() == size);
  return true;
}

bool Section::SeralizeTo(butil::IOBuf* buf) const {
  if (type == 0) {
    // Body
    if (!body_document) {
      return false;
    }
    uint8_t kind = 0;
    buf->append(static_cast<void*>(&kind), 1);
    buf->append(static_cast<const void*>(bson_get_data(body_document.get())),
                body_document.get()->len);
    return true;
  } else if (type == 1) {
    // Document Sequence
    if (!document_sequence) {
      return false;
    }
    uint8_t kind = 1;
    butil::IOBuf buf2;
    bool ret = document_sequence->SerializeTo(&buf2);
    if (!ret) {
      return false;
    }
    buf->append(static_cast<void*>(&kind), 1);
    buf->append(buf2);
    return true;
  } else {
    return false;
  }
}

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
  BsonPtr query_element_ptr = butil::bson::new_bson();
  bson_t* query_element = query_element_ptr.get();
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
  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
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

MongoGetMoreRequest::MongoGetMoreRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoGetMoreRequest::~MongoGetMoreRequest() { SharedDtor(); }

MongoGetMoreRequest::MongoGetMoreRequest(const MongoGetMoreRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

void MongoGetMoreRequest::Swap(MongoGetMoreRequest* other) {}

bool MongoGetMoreRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoGetMoreRequest not initialize";
    return false;
  }
  BsonPtr get_more_element_ptr = butil::bson::new_bson();
  bson_t* get_more_element = get_more_element_ptr.get();
  // getMore
  BSON_APPEND_INT64(get_more_element, "getMore", cursorid());
  // collection
  BSON_APPEND_UTF8(get_more_element, "collection", collection().c_str());
  // batch_size
  if (has_batch_size()) {
    BSON_APPEND_DOUBLE(get_more_element, "batchSize",
                       static_cast<double>(batch_size()));
  }
  // $db
  BSON_APPEND_UTF8(get_more_element, "$db", database().c_str());
  // Message Flags 4bytes
  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);
  uint8_t kind = 0;  // Body kind
  buf->append(static_cast<void*>(&kind), 1);
  buf->append(static_cast<const void*>(bson_get_data(get_more_element)),
              get_more_element->len);
  return true;
}

MongoGetMoreRequest* MongoGetMoreRequest::New() const {
  return new MongoGetMoreRequest;
}

void MongoGetMoreRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetMoreRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoGetMoreRequest* source =
      dynamic_cast<const MongoGetMoreRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoGetMoreRequest::CopyFrom(const MongoGetMoreRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetMoreRequest::MergeFrom(const MongoGetMoreRequest& from) {
  GOOGLE_CHECK_NE(&from, this);
  if (from.has_database()) {
    set_database(from.database());
  }
  if (from.has_collection()) {
    set_collection(from.collection());
  }
  if (from.has_cursorid()) {
    set_cursorid(from.cursorid());
  }
  if (from.has_batch_size()) {
    set_batch_size(from.batch_size());
  }
  if (from.has_max_time_ms()) {
    set_max_time_ms(from.max_time_ms());
  }
  if (from.has_comment()) {
    set_comment(from.comment());
  }
}

void MongoGetMoreRequest::Clear() {
  clear_database();
  clear_collection();
  clear_cursorid();
  clear_batch_size();
  clear_max_time_ms();
  clear_comment();
}

bool MongoGetMoreRequest::IsInitialized() const {
  return has_database() && has_collection() && has_cursorid();
}

bool MongoGetMoreRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoGetMoreRequest";
  return true;
}

void MongoGetMoreRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoGetMoreRequest";
}

::google::protobuf::uint8* MongoGetMoreRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoGetMoreRequest::descriptor() {
  return MongoGetMoreRequestBase::descriptor();
}

void MongoGetMoreRequest::SharedCtor() {
  cursorid_ = 0;
  batch_size_ = 0;
  max_time_ms_ = 0;
  _cached_size_ = 0;
}

void MongoGetMoreRequest::SharedDtor() {}

void MongoGetMoreRequest::SetCachedSize(int size) const {
  _cached_size_ = size;
}

::google::protobuf::Metadata MongoGetMoreRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

MongoCountRequest::MongoCountRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoCountRequest::~MongoCountRequest() { SharedDtor(); }

MongoCountRequest::MongoCountRequest(const MongoCountRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoCountRequest& MongoCountRequest::operator=(const MongoCountRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoCountRequest::SharedCtor() {
  _cached_size_ = 0;
  skip_ = 0;
  limit_ = 0;
}

void MongoCountRequest::SharedDtor() {}

bool MongoCountRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoCountRequest not initialize";
    return false;
  }
  BsonPtr count_element_ptr = butil::bson::new_bson();
  bson_t* count_element = count_element_ptr.get();
  // count
  BSON_APPEND_UTF8(count_element, "count", collection().c_str());
  // query
  auto query_filter = query();
  if (!query_filter) {
    query_filter.reset(bson_new(), bson_free);
  }
  BSON_APPEND_DOCUMENT(count_element, "query", query_filter.get());
  // limit
  if (has_limit()) {
    BSON_APPEND_INT64(count_element, "limit", limit());
  }
  // skip
  if (has_skip()) {
    BSON_APPEND_INT64(count_element, "skip", skip());
  }
  // $db
  BSON_APPEND_UTF8(count_element, "$db", database().c_str());
  // Message Flags 4bytes
  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);
  uint8_t kind = 0;  // Body kind
  buf->append(static_cast<void*>(&kind), 1);
  buf->append(static_cast<const void*>(bson_get_data(count_element)),
              count_element->len);
  return true;
}

void MongoCountRequest::Swap(MongoCountRequest* other) {}

MongoCountRequest* MongoCountRequest::New() const {
  return new MongoCountRequest();
}

void MongoCountRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoCountRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoCountRequest* source =
      dynamic_cast<const MongoCountRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoCountRequest::CopyFrom(const MongoCountRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoCountRequest::MergeFrom(const MongoCountRequest& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_database()) {
    set_database(from.database());
  }

  if (from.has_collection()) {
    set_collection(from.collection());
  }

  if (from.has_query()) {
    set_query(from.query());
  }

  if (from.has_skip()) {
    set_skip(from.skip());
  }

  if (from.has_limit()) {
    set_limit(from.limit());
  }
}

void MongoCountRequest::Clear() {
  clear_database();
  clear_collection();
  clear_query();
  clear_skip();
  clear_limit();
}

bool MongoCountRequest::IsInitialized() const {
  return has_database() && has_collection();
}

bool MongoCountRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoCountRequest";
  return true;
}

void MongoCountRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoCountRequest";
}

::google::protobuf::uint8* MongoCountRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoCountRequest::descriptor() {
  return MongoCountRequestBase::descriptor();
}

::google::protobuf::Metadata MongoCountRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoCountRequest::SetCachedSize(int size) const { _cached_size_ = size; }

MongoCountResponse::MongoCountResponse() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoCountResponse::~MongoCountResponse() { SharedDtor(); }

MongoCountResponse::MongoCountResponse(const MongoCountResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoCountResponse& MongoCountResponse::operator=(
    const MongoCountResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoCountResponse::SharedCtor() {
  _cached_size_ = 0;
  number_ = 0;
}

void MongoCountResponse::SharedDtor() {}

bool MongoCountResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
}

void MongoCountResponse::Swap(MongoCountResponse* other) {}

MongoCountResponse* MongoCountResponse::New() const {
  return new MongoCountResponse();
}

void MongoCountResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoCountResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoCountResponse* source =
      dynamic_cast<const MongoCountResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoCountResponse::CopyFrom(const MongoCountResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoCountResponse::MergeFrom(const MongoCountResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_number()) {
    set_number(from.number());
  }
}

void MongoCountResponse::Clear() { clear_number(); }

bool MongoCountResponse::IsInitialized() const { return true; }

bool MongoCountResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoCountResponse";
  return true;
}

void MongoCountResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoCountResponse";
}

::google::protobuf::uint8* MongoCountResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoCountResponse::descriptor() {
  return MongoCountResponseBase::descriptor();
}

::google::protobuf::Metadata MongoCountResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoCountResponse::SetCachedSize(int size) const { _cached_size_ = size; }

MongoInsertRequest::MongoInsertRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoInsertRequest::~MongoInsertRequest() { SharedDtor(); }

MongoInsertRequest::MongoInsertRequest(const MongoInsertRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoInsertRequest& MongoInsertRequest::operator=(
    const MongoInsertRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoInsertRequest::SharedCtor() {
  _cached_size_ = 0;
  ordered_ = true;
}

void MongoInsertRequest::SharedDtor() {}

bool MongoInsertRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoInsertRequest not initialize";
    return false;
  }
  if (documents().size() == 0) {
    LOG(WARNING) << "To insert document null";
    return false;
  }
  // Message Flags 4bytes
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);

  BsonPtr insert_body_element_ptr = butil::bson::new_bson();
  bson_t* insert_body_element = insert_body_element_ptr.get();
  // insert
  BSON_APPEND_UTF8(insert_body_element, "insert", collection().c_str());
  // ordered
  BSON_APPEND_BOOL(insert_body_element, "ordered", ordered());
  // $db
  BSON_APPEND_UTF8(insert_body_element, "$db", database().c_str());

  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  Section section1;
  section1.type = 0;
  section1.body_document = insert_body_element_ptr;
  butil::IOBuf buf1;
  bool ret = section1.SeralizeTo(&buf1);
  if (!ret) {
    return false;
  }
  buf->append(buf1);
  // Section Kind(1byte): Document Sequence(1); SeqID: documents
  // 添加object_id
  for (auto document : documents()) {
    bson_t* doc = document.get();
    if (!butil::bson::bson_has_oid(document)) {
      bson_oid_t oid;
      bson_oid_init(&oid, nullptr);
      BSON_APPEND_OID(doc, "_id", &oid);
    }
  }
  Section section2;
  section2.type = 1;
  DocumentSequencePtr document_sequence = std::make_shared<DocumentSequence>();
  document_sequence->identifier = "documents";
  document_sequence->documents = documents();
  section2.document_sequence = document_sequence;
  butil::IOBuf buf2;
  ret = section2.SeralizeTo(&buf2);
  if (!ret) {
    return false;
  }
  buf->append(buf2);
  return true;
}

void MongoInsertRequest::Swap(MongoInsertRequest* other) {}

MongoInsertRequest* MongoInsertRequest::New() const {
  return new MongoInsertRequest();
}

void MongoInsertRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoInsertRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoInsertRequest* source =
      dynamic_cast<const MongoInsertRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoInsertRequest::CopyFrom(const MongoInsertRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoInsertRequest::MergeFrom(const MongoInsertRequest& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_database()) {
    set_database(from.database());
  }

  if (from.has_collection()) {
    set_collection(from.collection());
  }

  if (from.has_ordered()) {
    set_ordered(from.ordered());
  }

  documents_.insert(documents_.end(), from.documents().cbegin(),
                    from.documents().cend());
}

void MongoInsertRequest::Clear() {
  clear_database();
  clear_collection();
  clear_ordered();
  clear_documents();
}

bool MongoInsertRequest::IsInitialized() const {
  return has_database() && has_collection();
}

bool MongoInsertRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoInsertRequest";
  return true;
}

void MongoInsertRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoInsertRequest";
}

::google::protobuf::uint8* MongoInsertRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoInsertRequest::descriptor() {
  return MongoInsertRequestBase::descriptor();
}

::google::protobuf::Metadata MongoInsertRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoInsertRequest::SetCachedSize(int size) const { _cached_size_ = size; }

MongoInsertResponse::MongoInsertResponse() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoInsertResponse::~MongoInsertResponse() { SharedDtor(); }

MongoInsertResponse::MongoInsertResponse(const MongoInsertResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoInsertResponse& MongoInsertResponse::operator=(
    const MongoInsertResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoInsertResponse::SharedCtor() {
  _cached_size_ = 0;
  number_ = 0;
}

void MongoInsertResponse::SharedDtor() {}

bool MongoInsertResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
}

void MongoInsertResponse::Swap(MongoInsertResponse* other) {}

MongoInsertResponse* MongoInsertResponse::New() const {
  return new MongoInsertResponse();
}

void MongoInsertResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoInsertResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoInsertResponse* source =
      dynamic_cast<const MongoInsertResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoInsertResponse::CopyFrom(const MongoInsertResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoInsertResponse::MergeFrom(const MongoInsertResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_number()) {
    set_number(from.number());
  }

  write_errors_.insert(write_errors_.end(), from.write_errors().cbegin(),
                       from.write_errors().cend());
}

void MongoInsertResponse::Clear() {
  clear_number();
  clear_write_errors();
}

bool MongoInsertResponse::IsInitialized() const { return true; }

bool MongoInsertResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoInsertResponse";
  return true;
}

void MongoInsertResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoInsertResponse";
}

::google::protobuf::uint8* MongoInsertResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoInsertResponse::descriptor() {
  return MongoInsertResponseBase::descriptor();
}

::google::protobuf::Metadata MongoInsertResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoInsertResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

}  // namespace brpc
