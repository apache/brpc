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
project(rdma_performance C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

option(LINK_SO "Whether examples are linked dynamically" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

protobuf_generate_cpp(PROTO_SRC PROTO_HEADER test.proto)
set(BRPC_EXAMPLE_WITH_RDMA ON)
find_path(RDMA_INCLUDE_PATH NAMES infiniband/verbs.h)
find_library(RDMA_LIB NAMES ibverbs)
if ((NOT RDMA_INCLUDE_PATH) OR (NOT RDMA_LIB))
    message(FATAL_ERROR "Fail to find ibverbs")
endif()
list(APPEND DYNAMIC_LIB ${RDMA_LIB})

add_executable(client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(client)
add_executable(server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(server)

target_link_libraries(client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(server PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
