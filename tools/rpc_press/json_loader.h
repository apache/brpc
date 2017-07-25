// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: 2014-10-29 14:20

#ifndef BRPC_JSON_LOADER_H
#define BRPC_JSON_LOADER_H

#include <string>
#include <deque>
#include <google/protobuf/message.h>
#include <google/protobuf/compiler/importer.h>
#include <google/protobuf/dynamic_message.h>
#include <base/iobuf.h>

namespace brpc {

// This utility loads pb messages in json format from a file or string.
class JsonLoader {
public:
    JsonLoader(google::protobuf::compiler::Importer* importer, 
               google::protobuf::DynamicMessageFactory* factory,
               const std::string& service_name,
               const std::string& method_name);
    ~JsonLoader() {}

    // TODO(gejun): messages should be lazily loaded.
    
    // Load jsons from fd or string, convert them into pb messages, then insert
    // them into `out_msgs'.
    void load_messages(int fd, std::deque<google::protobuf::Message*>* out_msgs);
    void load_messages(const std::string& jsons,
                       std::deque<google::protobuf::Message*>* out_msgs);
    
private:
    class Reader;

    void load_messages(
        JsonLoader::Reader* ctx,
        std::deque<google::protobuf::Message*>* out_msgs);

    google::protobuf::compiler::Importer* _importer;
    google::protobuf::DynamicMessageFactory* _factory;
    std::string _service_name;
    std::string _method_name;
    const google::protobuf::Message* _request_prototype;
};

} // namespace brpc

#endif // BRPC_JSON_LOADER_H
