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

file(GLOB_RECURSE BRPC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE THRIFT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/thrift*.cpp")
file(GLOB_RECURSE EXCLUDE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/event_dispatcher_*.cpp")

# When building with the real liburma, exclude the link-time mock so its urma_*
# symbols do not clash with the library. When liburma is absent, keep it so CI
# can build and test UrmaTransport without URMA hardware.
if(WITH_URMA AND NOT URMA_USE_MOCK_ENABLED)
    list(REMOVE_ITEM BRPC_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/urma/mock_urma.cpp")
endif()

if(WITH_THRIFT)
    message("brpc compile with thrift protocol")
else()
    # Remove thrift sources
    foreach(v ${THRIFT_SOURCES})
        list(REMOVE_ITEM BRPC_SOURCES ${v})
    endforeach()
    set(THRIFT_SOURCES "")
endif()

foreach(v ${EXCLUDE_SOURCES})
    list(REMOVE_ITEM BRPC_SOURCES ${v})
endforeach()

include(CompileProto)
set(PROTO_FILES brpc/builtin_service.proto
                brpc/errno.proto
                brpc/get_favicon.proto
                brpc/get_js.proto
                brpc/grpc_health_check.proto
                brpc/nshead_meta.proto
                brpc/options.proto
                brpc/policy/baidu_rpc_meta.proto
                brpc/policy/hulu_pbrpc_meta.proto
                brpc/policy/mongo.proto
                brpc/policy/public_pbrpc_meta.proto
                brpc/policy/sofa_pbrpc_meta.proto
                brpc/proto_base.proto
                brpc/rdma/rdma_handshake.proto
                brpc/rpc_dump.proto
                brpc/rtmp.proto
                brpc/span.proto
                brpc/streaming_rpc_meta.proto
                brpc/trackme.proto
                brpc/urma/urma_handshake.proto
                idl_options.proto)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/output/include/brpc)
set(PROTOC_FLAGS ${PROTOC_FLAGS} -I${PROTOBUF_INCLUDE_DIR})
compile_proto(PROTO_HDRS PROTO_SRCS ${PROJECT_BINARY_DIR}
                                    ${PROJECT_BINARY_DIR}/output/include
                                    ${CMAKE_CURRENT_SOURCE_DIR}/..
                                    "${PROTO_FILES}")
add_library(PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET PROTO_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(PROTO_LIB PRIVATE brpc_common_config)

set(BRPC_SOURCES ${BRPC_SOURCES} PARENT_SCOPE)
set(THRIFT_SOURCES ${THRIFT_SOURCES} PARENT_SCOPE)
set(PROTOC_FLAGS ${PROTOC_FLAGS} PARENT_SCOPE)
