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

#include <string>
#include <google/protobuf/compiler/command_line_interface.h>
#include "brpc/controller.h"
#include "butil/iobuf.h"
#include "butil/logging.h"
#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL
#include <thrift/TApplicationException.h>
#endif

int main() {
    LOG(INFO) << "Check the bundle's transitive link dependencies";
    google::protobuf::compiler::CommandLineInterface compiler;
    compiler.AllowPlugins("protoc-");
    brpc::Controller controller;
    butil::IOBuf buffer;
    if (buffer.append("link check") != 0 || controller.Failed()) {
        return 1;
    }
#ifdef ENABLE_THRIFT_FRAMED_PROTOCOL
    apache::thrift::TApplicationException exception("link check");
    if (std::string(exception.what()).find("link check") == std::string::npos) {
        return 1;
    }
#endif
    return buffer.to_string() != "link check";
}
