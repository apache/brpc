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

cmake_minimum_required(VERSION 3.16...3.28)
project(urma_performance C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

option(LINK_SO "Whether examples are linked dynamically" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

protobuf_generate_cpp(PROTO_SRC PROTO_HEADER test.proto)
set(BRPC_EXAMPLE_WITH_URMA ON)
find_library(URMA_LIB NAMES urma)
if(URMA_LIB)
    list(APPEND DYNAMIC_LIB ${URMA_LIB})
else()
    message(STATUS
        "liburma not found; using the URMA implementation linked into brpc")
endif()

add_executable(urma_performance_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(urma_performance_client)
add_executable(urma_performance_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(urma_performance_server)

target_link_libraries(urma_performance_client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(urma_performance_server PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
