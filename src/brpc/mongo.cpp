#include "brpc/mongo.h"

namespace brpc {

policy::MongoDBRequest MakeMongoInsertRequest() {
  policy::MongoDBRequest request;
  request.set_op(policy::DB_INSERT);
  request.mutable_insert();
  return request;
}

policy::MongoDBRequest MakeMongoQueryRequest() {
  policy::MongoDBRequest request;
  request.set_op(policy::DB_QUERY);
  request.mutable_query();
  return request;
}

void SerializeMongoSection(mongo_section_t *section, std::string *out) {
  assert(out);
  out->clear();
  out->resize(sizeof(section->type) + section->data.size());
  char *data = const_cast<char *>(out->c_str());
  memcpy(data, &(section->type), sizeof(section->type));
  memcpy(data + sizeof(section->type), section->data.c_str(), section->data.size());
}

void SerializeMongoMsg(mongo_msg_t *msg, std::string *out) {
  // std::vector<std::string> sections_data;
  out->resize(sizeof(msg->flag_bits));
  memcpy(const_cast<char *>(out->data()), &(msg->flag_bits), sizeof(msg->flag_bits));
  for (size_t i = 0; i < msg->sections.size(); ++i) {
    std::string section_data;
    SerializeMongoSection(&(msg->sections[i]), &section_data);
    out->append(section_data);
    // sections_data.push_back(std::move(section_data));
  }
}

}  // namespace brpc
