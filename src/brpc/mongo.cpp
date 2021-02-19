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

MongoQueryRequest::~MongoQueryRequest() { SharedDtor(); }

MongoQueryRequest::MongoQueryRequest(const MongoQueryRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoQueryRequest& MongoQueryRequest::operator=(const MongoQueryRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoQueryRequest::SharedCtor() {
  _cached_size_ = 0;
  skip_ = 0;
  limit_ = 0;
}

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
  // query sort
  if (sort()) {
    BSON_APPEND_DOCUMENT(query_element, "sort", sort().get());
  }
  if (!fields().empty()) {
    // 是否需要bson_free
    BsonPtr field_doc_ptr = butil::bson::new_bson();
    for (auto& field : fields()) {
      BSON_APPEND_INT32(field_doc_ptr.get(), field.c_str(), 1);
    }
    BSON_APPEND_DOCUMENT(query_element, "projection", field_doc_ptr.get());
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

void MongoQueryRequest::Swap(MongoQueryRequest* other) {}

MongoQueryRequest* MongoQueryRequest::New() const {
  return new MongoQueryRequest();
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

  if (from.has_query()) {
    set_query(from.query());
  }

  if (from.has_sort()) {
    set_sort(from.sort());
  }

  if (from.has_skip()) {
    set_skip(from.skip());
  }

  if (from.has_limit()) {
    set_limit(from.limit());
  }

  fields_.insert(fields_.end(), from.fields().cbegin(), from.fields().cend());
}

void MongoQueryRequest::Clear() {
  clear_database();
  clear_collection();
  clear_query();
  clear_sort();
  clear_skip();
  clear_limit();
  clear_fields();
}

bool MongoQueryRequest::IsInitialized() const {
  return has_database() && has_collection();
}

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

::google::protobuf::Metadata MongoQueryRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoQueryRequest::SetCachedSize(int size) const { _cached_size_ = size; }

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
  return true;
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
  return true;
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

MongoDeleteRequest::MongoDeleteRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoDeleteRequest::~MongoDeleteRequest() { SharedDtor(); }

MongoDeleteRequest::MongoDeleteRequest(const MongoDeleteRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoDeleteRequest& MongoDeleteRequest::operator=(
    const MongoDeleteRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoDeleteRequest::SharedCtor() {
  _cached_size_ = 0;
  ordered_ = false;
  delete_many_ = false;
}

void MongoDeleteRequest::SharedDtor() {}

bool MongoDeleteRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoDeleteRequest not initialize";
    return false;
  }
  // Message Flags 4bytes
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);

  BsonPtr delete_body_element_ptr = butil::bson::new_bson();
  bson_t* delete_body_element = delete_body_element_ptr.get();
  // delete
  BSON_APPEND_UTF8(delete_body_element, "delete", collection().c_str());
  // ordered
  BSON_APPEND_BOOL(delete_body_element, "ordered", ordered());
  // $db
  BSON_APPEND_UTF8(delete_body_element, "$db", database().c_str());

  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  Section section1;
  section1.type = 0;
  section1.body_document = delete_body_element_ptr;
  butil::IOBuf buf1;
  bool ret = section1.SeralizeTo(&buf1);
  if (!ret) {
    return false;
  }
  buf->append(buf1);
  // Section Kind(1byte): Document Sequence(1); SeqID: deletes
  Section section2;
  section2.type = 1;
  DocumentSequencePtr document_sequence = std::make_shared<DocumentSequence>();
  document_sequence->identifier = "deletes";
  // 删除记录的查询条件
  BsonPtr delete_filter_element_ptr = butil::bson::new_bson();
  BsonPtr empty_query_ptr;
  if (query()) {
    BSON_APPEND_DOCUMENT(delete_filter_element_ptr.get(), "q", query().get());
  } else {
    empty_query_ptr = butil::bson::new_bson();
    BSON_APPEND_DOCUMENT(delete_filter_element_ptr.get(), "q",
                         empty_query_ptr.get());
  }
  document_sequence->documents.push_back(delete_filter_element_ptr);
  // 限制删除的数目, 0不限制, 1只删除一条
  BSON_APPEND_INT32(delete_filter_element_ptr.get(), "limit",
                    (delete_many() ? 0 : 1));
  section2.document_sequence = document_sequence;
  butil::IOBuf buf2;
  ret = section2.SeralizeTo(&buf2);
  if (!ret) {
    return false;
  }
  buf->append(buf2);
  return true;
}

void MongoDeleteRequest::Swap(MongoDeleteRequest* other) {}

MongoDeleteRequest* MongoDeleteRequest::New() const {
  return new MongoDeleteRequest();
}

void MongoDeleteRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoDeleteRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoDeleteRequest* source =
      dynamic_cast<const MongoDeleteRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoDeleteRequest::CopyFrom(const MongoDeleteRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoDeleteRequest::MergeFrom(const MongoDeleteRequest& from) {
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

  if (from.has_query()) {
    set_query(from.query());
  }

  if (from.has_delete_many()) {
    set_delete_many(from.delete_many());
  }
}

void MongoDeleteRequest::Clear() {
  clear_database();
  clear_collection();
  clear_ordered();
  clear_query();
  clear_delete_many();
}

bool MongoDeleteRequest::IsInitialized() const {
  return has_database() && has_collection();
}

bool MongoDeleteRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoDeleteRequest";
  return true;
}

void MongoDeleteRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoDeleteRequest";
}

::google::protobuf::uint8* MongoDeleteRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoDeleteRequest::descriptor() {
  return MongoDeleteRequestBase::descriptor();
}

::google::protobuf::Metadata MongoDeleteRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoDeleteRequest::SetCachedSize(int size) const { _cached_size_ = size; }

MongoDeleteResponse::MongoDeleteResponse() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoDeleteResponse::~MongoDeleteResponse() { SharedDtor(); }

MongoDeleteResponse::MongoDeleteResponse(const MongoDeleteResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoDeleteResponse& MongoDeleteResponse::operator=(
    const MongoDeleteResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoDeleteResponse::SharedCtor() {
  _cached_size_ = 0;
  number_ = 0;
}

void MongoDeleteResponse::SharedDtor() {}

bool MongoDeleteResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
  return true;
}

void MongoDeleteResponse::Swap(MongoDeleteResponse* other) {}

MongoDeleteResponse* MongoDeleteResponse::New() const {
  return new MongoDeleteResponse();
}

void MongoDeleteResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoDeleteResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoDeleteResponse* source =
      dynamic_cast<const MongoDeleteResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoDeleteResponse::CopyFrom(const MongoDeleteResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoDeleteResponse::MergeFrom(const MongoDeleteResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_number()) {
    set_number(from.number());
  }
}

void MongoDeleteResponse::Clear() { clear_number(); }

bool MongoDeleteResponse::IsInitialized() const { return true; }

bool MongoDeleteResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoDeleteResponse";
  return true;
}

void MongoDeleteResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoDeleteResponse";
}

::google::protobuf::uint8* MongoDeleteResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoDeleteResponse::descriptor() {
  return MongoDeleteResponseBase::descriptor();
}

::google::protobuf::Metadata MongoDeleteResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoDeleteResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

MongoUpdateRequest::MongoUpdateRequest() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoUpdateRequest::~MongoUpdateRequest() { SharedDtor(); }

MongoUpdateRequest::MongoUpdateRequest(const MongoUpdateRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoUpdateRequest& MongoUpdateRequest::operator=(
    const MongoUpdateRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoUpdateRequest::SharedCtor() {
  _cached_size_ = 0;
  ordered_ = true;
  upsert_ = false;
  multi_ = false;
}

void MongoUpdateRequest::SharedDtor() {}

bool MongoUpdateRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoUpdateRequest not initialize";
    return false;
  }
  // $ operators
  bson_iter_t iter;
  if (!bson_iter_init(&iter, update().get())) {
    LOG(ERROR) << "update document is corrupt";
    return false;
  }
  while (bson_iter_next(&iter)) {
    const char* key = bson_iter_key(&iter);
    if (key[0] != '$') {
      LOG(ERROR) << "update only works with $ operators";
      return false;
    }
  }

  // Message Flags 4bytes
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);

  BsonPtr update_body_element_ptr = butil::bson::new_bson();
  bson_t* update_body_element = update_body_element_ptr.get();
  // update
  BSON_APPEND_UTF8(update_body_element, "update", collection().c_str());
  // ordered
  BSON_APPEND_BOOL(update_body_element, "ordered", ordered());
  // $db
  BSON_APPEND_UTF8(update_body_element, "$db", database().c_str());

  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  Section section1;
  section1.type = 0;
  section1.body_document = update_body_element_ptr;
  butil::IOBuf buf1;
  bool ret = section1.SeralizeTo(&buf1);
  if (!ret) {
    return false;
  }
  buf->append(buf1);
  // Section Kind(1byte): Document Sequence(1); SeqID: updates
  Section section2;
  section2.type = 1;
  DocumentSequencePtr document_sequence = std::make_shared<DocumentSequence>();
  document_sequence->identifier = "updates";
  // 更新记录的查询条件
  BsonPtr update_selector_element_ptr = butil::bson::new_bson();
  BSON_APPEND_DOCUMENT(update_selector_element_ptr.get(), "q",
                       selector().get());
  BSON_APPEND_DOCUMENT(update_selector_element_ptr.get(), "u", update().get());
  BSON_APPEND_BOOL(update_selector_element_ptr.get(), "upsert", upsert());
  BSON_APPEND_BOOL(update_selector_element_ptr.get(), "multi", multi());
  document_sequence->documents.push_back(update_selector_element_ptr);
  section2.document_sequence = document_sequence;
  butil::IOBuf buf2;
  ret = section2.SeralizeTo(&buf2);
  if (!ret) {
    return false;
  }
  buf->append(buf2);
  return true;
}

void MongoUpdateRequest::Swap(MongoUpdateRequest* other) {}

MongoUpdateRequest* MongoUpdateRequest::New() const {
  return new MongoUpdateRequest();
}

void MongoUpdateRequest::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoUpdateRequest::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoUpdateRequest* source =
      dynamic_cast<const MongoUpdateRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoUpdateRequest::CopyFrom(const MongoUpdateRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoUpdateRequest::MergeFrom(const MongoUpdateRequest& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_database()) {
    set_database(from.database());
  }

  if (from.has_collection()) {
    set_collection(from.collection());
  }

  if (from.has_selector()) {
    set_selector(from.selector());
  }

  if (from.has_update()) {
    set_update(from.update());
  }

  if (from.has_ordered()) {
    set_ordered(from.ordered());
  }

  if (from.has_upsert()) {
    set_upsert(from.upsert());
  }

  if (from.has_multi()) {
    set_multi(from.multi());
  }
}

void MongoUpdateRequest::Clear() {
  clear_database();
  clear_collection();
  clear_selector();
  clear_update();
  clear_ordered();
  clear_upsert();
  clear_multi();
}

bool MongoUpdateRequest::IsInitialized() const {
  return has_database() && has_collection() && has_selector() && has_update();
}

bool MongoUpdateRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoUpdateRequest";
  return true;
}

void MongoUpdateRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoUpdateRequest";
}

::google::protobuf::uint8* MongoUpdateRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoUpdateRequest::descriptor() {
  return MongoUpdateRequestBase::descriptor();
}

::google::protobuf::Metadata MongoUpdateRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoUpdateRequest::SetCachedSize(int size) const { _cached_size_ = size; }

MongoUpdateResponse::MongoUpdateResponse() : ::google::protobuf::Message() {
  SharedCtor();
}

MongoUpdateResponse::~MongoUpdateResponse() { SharedDtor(); }

MongoUpdateResponse::MongoUpdateResponse(const MongoUpdateResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoUpdateResponse& MongoUpdateResponse::operator=(
    const MongoUpdateResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoUpdateResponse::SharedCtor() {
  _cached_size_ = 0;
  matched_number_ = 0;
  modified_number_ = 0;
}

void MongoUpdateResponse::SharedDtor() {}

bool MongoUpdateResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
  return true;
}

void MongoUpdateResponse::Swap(MongoUpdateResponse* other) {}

MongoUpdateResponse* MongoUpdateResponse::New() const {
  return new MongoUpdateResponse();
}

void MongoUpdateResponse::CopyFrom(const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoUpdateResponse::MergeFrom(const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoUpdateResponse* source =
      dynamic_cast<const MongoUpdateResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoUpdateResponse::CopyFrom(const MongoUpdateResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoUpdateResponse::MergeFrom(const MongoUpdateResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_matched_number()) {
    set_matched_number(from.matched_number());
  }

  if (from.has_modified_number()) {
    set_modified_number(from.modified_number());
  }

  upserted_docs_.insert(upserted_docs_.end(), from.upserted_docs().cbegin(),
                        from.upserted_docs().cend());

  write_errors_.insert(write_errors_.end(), from.write_errors().cbegin(),
                       from.write_errors().cend());
}

void MongoUpdateResponse::Clear() {
  clear_matched_number();
  clear_modified_number();
  clear_upserted_docs();
  clear_write_errors();
}

bool MongoUpdateResponse::IsInitialized() const { return true; }

bool MongoUpdateResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoUpdateResponse";
  return true;
}

void MongoUpdateResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING) << "You're not supposed to serialize a MongoUpdateResponse";
}

::google::protobuf::uint8* MongoUpdateResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoUpdateResponse::descriptor() {
  return MongoUpdateResponseBase::descriptor();
}

::google::protobuf::Metadata MongoUpdateResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoUpdateResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

MongoFindAndModifyRequest::MongoFindAndModifyRequest()
    : ::google::protobuf::Message() {
  SharedCtor();
}

MongoFindAndModifyRequest::~MongoFindAndModifyRequest() { SharedDtor(); }

MongoFindAndModifyRequest::MongoFindAndModifyRequest(
    const MongoFindAndModifyRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoFindAndModifyRequest& MongoFindAndModifyRequest::operator=(
    const MongoFindAndModifyRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoFindAndModifyRequest::SharedCtor() {
  _cached_size_ = 0;
  upsert_ = false;
  remove_ = false;
  return_new_ = false;
}

void MongoFindAndModifyRequest::SharedDtor() {}

bool MongoFindAndModifyRequest::SerializeTo(butil::IOBuf* buf) const {
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoFindAndModifyRequest not initialize";
    return false;
  }
  if (has_remove() && remove()) {
    if (has_update()) {
      LOG(ERROR) << "MongoFindAndModifyRequest cannot specify both update and "
                    "remove=true";
      return false;
    }
    if (has_upsert() && upsert()) {
      LOG(ERROR) << "MongoFindAndModifyRequest cannot specify both upsert=true "
                    "and remove=true";
      return false;
    }
    if (has_return_new() && return_new()) {
      LOG(ERROR) << "MongoFindAndModifyRequest cannot specify both new=true "
                    "and remove=true";
      return false;
    }
  }
  if (has_update()) {
    // $ operators
    bson_iter_t iter;
    if (!bson_iter_init(&iter, update().get())) {
      LOG(ERROR) << "update document is corrupt";
      return false;
    }
    while (bson_iter_next(&iter)) {
      const char* key = bson_iter_key(&iter);
      if (key[0] != '$') {
        LOG(ERROR) << "update only works with $ operators";
        return false;
      }
    }
  }

  // Message Flags 4bytes
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);

  BsonPtr find_and_modify_element_ptr = butil::bson::new_bson();
  bson_t* find_and_modify_element = find_and_modify_element_ptr.get();
  // findAndModify
  BSON_APPEND_UTF8(find_and_modify_element, "findAndModify",
                   collection().c_str());
  // query
  BSON_APPEND_DOCUMENT(find_and_modify_element, "query", query().get());
  // update
  if (has_update()) {
    BSON_APPEND_DOCUMENT(find_and_modify_element, "update", update().get());
  }
  // upsert
  BSON_APPEND_BOOL(find_and_modify_element, "upsert", upsert());
  // new
  BSON_APPEND_BOOL(find_and_modify_element, "new", return_new());
  // remove
  BSON_APPEND_BOOL(find_and_modify_element, "remove", remove());
  // $db
  BSON_APPEND_UTF8(find_and_modify_element, "$db", database().c_str());

  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  Section section1;
  section1.type = 0;
  section1.body_document = find_and_modify_element_ptr;
  butil::IOBuf buf1;
  bool ret = section1.SeralizeTo(&buf1);
  if (!ret) {
    return false;
  }
  buf->append(buf1);
  return true;
}

void MongoFindAndModifyRequest::Swap(MongoFindAndModifyRequest* other) {}

MongoFindAndModifyRequest* MongoFindAndModifyRequest::New() const {
  return new MongoFindAndModifyRequest();
}

void MongoFindAndModifyRequest::CopyFrom(
    const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoFindAndModifyRequest::MergeFrom(
    const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoFindAndModifyRequest* source =
      dynamic_cast<const MongoFindAndModifyRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoFindAndModifyRequest::CopyFrom(
    const MongoFindAndModifyRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoFindAndModifyRequest::MergeFrom(
    const MongoFindAndModifyRequest& from) {
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

  if (from.has_sort()) {
    set_sort(from.sort());
  }

  if (from.has_update()) {
    set_update(from.update());
  }

  if (from.has_upsert()) {
    set_upsert(from.upsert());
  }

  if (from.has_remove()) {
    set_remove(from.remove());
  }

  if (from.has_return_new()) {
    set_return_new(from.return_new());
  }

  fields_.insert(fields_.end(), from.fields().cbegin(), from.fields().cend());
}

void MongoFindAndModifyRequest::Clear() {
  clear_database();
  clear_collection();
  clear_query();
  clear_sort();
  clear_update();
  clear_upsert();
  clear_remove();
  clear_return_new();
  clear_fields();
}

bool MongoFindAndModifyRequest::IsInitialized() const {
  return has_database() && has_collection() && has_query() &&
         (has_update() || has_remove());
}

bool MongoFindAndModifyRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoFindAndModifyRequest";
  return true;
}

void MongoFindAndModifyRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING)
      << "You're not supposed to serialize a MongoFindAndModifyRequest";
}

::google::protobuf::uint8*
MongoFindAndModifyRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoFindAndModifyRequest::descriptor() {
  return MongoFindAndModifyRequestBase::descriptor();
}

::google::protobuf::Metadata MongoFindAndModifyRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoFindAndModifyRequest::SetCachedSize(int size) const {
  _cached_size_ = size;
}

MongoFindAndModifyResponse::MongoFindAndModifyResponse()
    : ::google::protobuf::Message() {
  SharedCtor();
}

MongoFindAndModifyResponse::~MongoFindAndModifyResponse() { SharedDtor(); }

MongoFindAndModifyResponse::MongoFindAndModifyResponse(
    const MongoFindAndModifyResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoFindAndModifyResponse& MongoFindAndModifyResponse::operator=(
    const MongoFindAndModifyResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoFindAndModifyResponse::SharedCtor() {
  _cached_size_ = 0;
  memset(&upserted_, 0, sizeof(upserted_));
}

void MongoFindAndModifyResponse::SharedDtor() {}

bool MongoFindAndModifyResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
  return true;
}

void MongoFindAndModifyResponse::Swap(MongoFindAndModifyResponse* other) {}

MongoFindAndModifyResponse* MongoFindAndModifyResponse::New() const {
  return new MongoFindAndModifyResponse();
}

void MongoFindAndModifyResponse::CopyFrom(
    const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoFindAndModifyResponse::MergeFrom(
    const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoFindAndModifyResponse* source =
      dynamic_cast<const MongoFindAndModifyResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoFindAndModifyResponse::CopyFrom(
    const MongoFindAndModifyResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoFindAndModifyResponse::MergeFrom(
    const MongoFindAndModifyResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_value()) {
    set_value(from.value());
  }

  if (from.has_upserted()) {
    set_upserted(from.upserted());
  }
}

void MongoFindAndModifyResponse::Clear() {
  clear_value();
  clear_upserted();
}

bool MongoFindAndModifyResponse::IsInitialized() const { return true; }

bool MongoFindAndModifyResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoFindAndModifyResponse";
  return true;
}

void MongoFindAndModifyResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING)
      << "You're not supposed to serialize a MongoFindAndModifyResponse";
}

::google::protobuf::uint8*
MongoFindAndModifyResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor* MongoFindAndModifyResponse::descriptor() {
  return MongoFindAndModifyResponseBase::descriptor();
}

::google::protobuf::Metadata MongoFindAndModifyResponse::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoFindAndModifyResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

MongoGetReplSetStatusRequest::MongoGetReplSetStatusRequest()
    : ::google::protobuf::Message() {
  SharedCtor();
}

MongoGetReplSetStatusRequest::~MongoGetReplSetStatusRequest() { SharedDtor(); }

MongoGetReplSetStatusRequest::MongoGetReplSetStatusRequest(
    const MongoGetReplSetStatusRequest& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoGetReplSetStatusRequest& MongoGetReplSetStatusRequest::operator=(
    const MongoGetReplSetStatusRequest& from) {
  CopyFrom(from);
  return *this;
}

void MongoGetReplSetStatusRequest::SharedCtor() { _cached_size_ = 0; }

void MongoGetReplSetStatusRequest::SharedDtor() {}

bool MongoGetReplSetStatusRequest::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
  if (!IsInitialized()) {
    LOG(WARNING) << "MongoGetReplSetStatusRequest not initialize";
    return false;
  }

  // Message Flags 4bytes
  uint32_t flag_bits = 0;
  buf->append(static_cast<void*>(&flag_bits), 4);

  BsonPtr get_repl_set_status_element_ptr = butil::bson::new_bson();
  bson_t* get_repl_set_status_element = get_repl_set_status_element_ptr.get();
  // replSetGetStatus
  BSON_APPEND_DOUBLE(get_repl_set_status_element, "replSetGetStatus", 1.0);
  // $db
  BSON_APPEND_UTF8(get_repl_set_status_element, "$db", "admin");

  // Section[]  Kind(1byte): Body(0); BodyDocument(Bson)
  Section section1;
  section1.type = 0;
  section1.body_document = get_repl_set_status_element_ptr;
  butil::IOBuf buf1;
  bool ret = section1.SeralizeTo(&buf1);
  if (!ret) {
    return false;
  }
  buf->append(buf1);
  return true;
}

void MongoGetReplSetStatusRequest::Swap(MongoGetReplSetStatusRequest* other) {}

MongoGetReplSetStatusRequest* MongoGetReplSetStatusRequest::New() const {
  return new MongoGetReplSetStatusRequest();
}

void MongoGetReplSetStatusRequest::CopyFrom(
    const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetReplSetStatusRequest::MergeFrom(
    const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoGetReplSetStatusRequest* source =
      dynamic_cast<const MongoGetReplSetStatusRequest*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoGetReplSetStatusRequest::CopyFrom(
    const MongoGetReplSetStatusRequest& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetReplSetStatusRequest::MergeFrom(
    const MongoGetReplSetStatusRequest& from) {
  GOOGLE_CHECK_NE(&from, this);
}

void MongoGetReplSetStatusRequest::Clear() {}

bool MongoGetReplSetStatusRequest::IsInitialized() const { return true; }

bool MongoGetReplSetStatusRequest::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING) << "You're not supposed to parse a MongoGetReplSetStatusRequest";
  return true;
}

void MongoGetReplSetStatusRequest::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING)
      << "You're not supposed to serialize a MongoGetReplSetStatusRequest";
}

::google::protobuf::uint8*
MongoGetReplSetStatusRequest::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor*
MongoGetReplSetStatusRequest::descriptor() {
  return MongoGetReplSetStatusRequestBase::descriptor();
}

::google::protobuf::Metadata MongoGetReplSetStatusRequest::GetMetadata() const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoGetReplSetStatusRequest::SetCachedSize(int size) const {
  _cached_size_ = size;
}

MongoGetReplSetStatusResponse::MongoGetReplSetStatusResponse()
    : ::google::protobuf::Message() {
  SharedCtor();
}

MongoGetReplSetStatusResponse::~MongoGetReplSetStatusResponse() {
  SharedDtor();
}

MongoGetReplSetStatusResponse::MongoGetReplSetStatusResponse(
    const MongoGetReplSetStatusResponse& from)
    : ::google::protobuf::Message() {
  SharedCtor();
  MergeFrom(from);
}

MongoGetReplSetStatusResponse& MongoGetReplSetStatusResponse::operator=(
    const MongoGetReplSetStatusResponse& from) {
  CopyFrom(from);
  return *this;
}

void MongoGetReplSetStatusResponse::SharedCtor() {
  _cached_size_ = 0;
  ok_ = false;
  myState_ = 0;
}

void MongoGetReplSetStatusResponse::SharedDtor() {}

bool MongoGetReplSetStatusResponse::SerializeTo(butil::IOBuf* buf) const {
  // TODO custom definetion
  return true;
}

void MongoGetReplSetStatusResponse::Swap(MongoGetReplSetStatusResponse* other) {
}

MongoGetReplSetStatusResponse* MongoGetReplSetStatusResponse::New() const {
  return new MongoGetReplSetStatusResponse();
}

void MongoGetReplSetStatusResponse::CopyFrom(
    const ::google::protobuf::Message& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetReplSetStatusResponse::MergeFrom(
    const ::google::protobuf::Message& from) {
  GOOGLE_CHECK_NE(&from, this);
  const MongoGetReplSetStatusResponse* source =
      dynamic_cast<const MongoGetReplSetStatusResponse*>(&from);
  if (source == NULL) {
    ::google::protobuf::internal::ReflectionOps::Merge(from, this);
  } else {
    MergeFrom(*source);
  }
}

void MongoGetReplSetStatusResponse::CopyFrom(
    const MongoGetReplSetStatusResponse& from) {
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

void MongoGetReplSetStatusResponse::MergeFrom(
    const MongoGetReplSetStatusResponse& from) {
  GOOGLE_CHECK_NE(&from, this);

  if (from.has_ok()) {
    set_ok(from.ok());
  }

  if (from.has_set()) {
    set_set(from.set());
  }

  if (from.has_myState()) {
    set_myState(from.myState());
  }

  members_.insert(members_.end(), from.members_.cbegin(), from.members_.cend());
}

void MongoGetReplSetStatusResponse::Clear() {
  clear_ok();
  clear_set();
  clear_myState();
  clear_members();
}

bool MongoGetReplSetStatusResponse::IsInitialized() const { return has_ok(); }

bool MongoGetReplSetStatusResponse::MergePartialFromCodedStream(
    ::google::protobuf::io::CodedInputStream* input) {
  LOG(WARNING)
      << "You're not supposed to parse a MongoGetReplSetStatusResponse";
  return true;
}

void MongoGetReplSetStatusResponse::SerializeWithCachedSizes(
    ::google::protobuf::io::CodedOutputStream* output) const {
  LOG(WARNING)
      << "You're not supposed to serialize a MongoGetReplSetStatusResponse";
}

::google::protobuf::uint8*
MongoGetReplSetStatusResponse::SerializeWithCachedSizesToArray(
    ::google::protobuf::uint8* output) const {
  return output;
}

const ::google::protobuf::Descriptor*
MongoGetReplSetStatusResponse::descriptor() {
  return MongoGetReplSetStatusResponseBase::descriptor();
}

::google::protobuf::Metadata MongoGetReplSetStatusResponse::GetMetadata()
    const {
  ::google::protobuf::Metadata metadata;
  metadata.descriptor = descriptor();
  metadata.reflection = NULL;
  return metadata;
}

void MongoGetReplSetStatusResponse::SetCachedSize(int size) const {
  _cached_size_ = size;
}

}  // namespace brpc