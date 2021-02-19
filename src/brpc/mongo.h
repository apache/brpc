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

#include <string>
#include "brpc/mongo_head.h"
#include "brpc/policy/mongo.pb.h"

namespace brpc {
  policy::MongoDBRequest MakeMongoInsertRequest();

  policy::MongoDBRequest MakeMongoQueryRequest();

  void SerializeMongoSection(mongo_section_t *section, std::string *out);

  void SerializeMongoMsg(mongo_msg_t *msg, std::string *out);
}  // namespace brpc

#endif  // BRPC_MONGO_H
