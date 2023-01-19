# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

include(FindProtobuf)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER view.proto)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(rpc_view rpc_view.cpp ${PROTO_SRC})
target_link_libraries(rpc_view brpc-static ${DYNAMIC_LIB})
install(TARGETS rpc_view RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
