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

set(MCPACK2PB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/field_type.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mcpack2pb.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/parser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer.cpp)

include(CompileProto)
compile_proto(IDL_OPTIONS_HDRS IDL_OPTIONS_SRCS ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    "idl_options.proto")
add_library(mcpack2pb_build_config INTERFACE)
target_include_directories(mcpack2pb_build_config BEFORE INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mcpack2pb_build_config INTERFACE butil_build_config)
add_library(IDL_OPTIONS_LIB OBJECT ${IDL_OPTIONS_SRCS} ${IDL_OPTIONS_HDRS})
set_property(TARGET IDL_OPTIONS_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(IDL_OPTIONS_LIB PRIVATE mcpack2pb_build_config)

add_library(MCPACK2PB_LIB OBJECT ${MCPACK2PB_SOURCES})
target_link_libraries(MCPACK2PB_LIB PRIVATE mcpack2pb_build_config)
set_property(TARGET MCPACK2PB_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)

if(TARGET protobuf::libprotoc)
    set(PROTOC_LIB protobuf::libprotoc)
else()
    find_library(PROTOC_LIB NAMES protoc)
endif()
if(NOT PROTOC_LIB)
    message(FATAL_ERROR "Fail to find protoc lib")
endif()

# Legacy bundles/tests retain protoc linkage; the runtime component does not.
set_property(TARGET MCPACK2PB_LIB PROPERTY BRPC_BUNDLE_LIBRARIES "${PROTOC_LIB}")
set_property(TARGET MCPACK2PB_LIB PROPERTY BRPC_PRIVATE_LIBS "-lprotoc")

add_executable(protoc-gen-mcpack generator.cpp)
add_dependencies(protoc-gen-mcpack IDL_OPTIONS_LIB)
set_target_properties(protoc-gen-mcpack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/output/bin)
target_link_libraries(protoc-gen-mcpack PRIVATE mcpack2pb_build_config)
if(BUILD_SHARED_LIBS)
    target_link_libraries(protoc-gen-mcpack PRIVATE brpc-shared brpc_bundle_dependencies)
else()
    target_link_libraries(protoc-gen-mcpack PRIVATE brpc-static brpc_bundle_dependencies)
endif()

if(BRPC_BUILD_COMPONENT_LIBS)
    brpc_add_component(mcpack2pb MCPACK2PB_LIB ${IDL_OPTIONS_HDRS})
    target_sources(mcpack2pb PRIVATE $<TARGET_OBJECTS:IDL_OPTIONS_LIB>)
    target_link_libraries(mcpack2pb PUBLIC brpc::butil protobuf::libprotobuf)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/idl_options.proto
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

brpc_install_module_headers()
if(NOT BRPC_BUILD_COMPONENT_LIBS)
    brpc_install_headers(${CMAKE_CURRENT_BINARY_DIR} ${IDL_OPTIONS_HDRS})
endif()
