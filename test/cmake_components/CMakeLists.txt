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

cmake_minimum_required(VERSION 3.16)
project(brpc_component_consumers CXX)
enable_testing()

# Exercise a narrow import, repeated imports, and transitive target loading.
find_package(brpc CONFIG QUIET COMPONENTS nonexistent)
if(brpc_FOUND)
    message(FATAL_ERROR "An unknown required component was accepted")
endif()
find_package(brpc CONFIG REQUIRED COMPONENTS bvar)
if(TARGET brpc::bthread OR TARGET brpc::json2pb OR TARGET brpc::mcpack2pb OR
   TARGET brpc::brpc OR TARGET brpc::leveldb)
    message(FATAL_ERROR "Importing bvar loaded unrelated component targets")
endif()
find_package(brpc CONFIG REQUIRED COMPONENTS bthread json2pb mcpack2pb)
find_package(brpc CONFIG REQUIRED COMPONENTS brpc)
find_package(brpc CONFIG REQUIRED)
foreach(component butil bvar bthread json2pb mcpack2pb brpc)
    add_executable(consume_${component} consumer.cpp)
    target_compile_definitions(consume_${component} PRIVATE TEST_${component})
    target_link_libraries(consume_${component} PRIVATE brpc::${component})
    add_test(NAME consume_${component} COMMAND consume_${component})
endforeach()

# Discover the installed/build-tree schema through the component's public
# include directories; do not add producer paths to the consumer build.
get_target_property(mcpack_include_dirs brpc::mcpack2pb INTERFACE_INCLUDE_DIRECTORIES)
find_file(IDL_OPTIONS_PROTO NAMES idl_options.proto
    PATHS ${mcpack_include_dirs} NO_DEFAULT_PATH)
if(NOT IDL_OPTIONS_PROTO)
    message(FATAL_ERROR "mcpack2pb did not expose idl_options.proto")
endif()
get_filename_component(idl_proto_dir ${IDL_OPTIONS_PROTO} DIRECTORY)
set(proto_include_args -I${CMAKE_CURRENT_SOURCE_DIR} -I${idl_proto_dir})
foreach(include_dir IN LISTS Protobuf_INCLUDE_DIRS)
    list(APPEND proto_include_args -I${include_dir})
endforeach()
foreach(component brpc mcpack2pb)
    if(component STREQUAL "brpc")
        set(proto_name echo)
    else()
        set(proto_name mcpack)
    endif()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.cc
                              ${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.h
        COMMAND protobuf::protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
            ${proto_include_args} ${CMAKE_CURRENT_SOURCE_DIR}/${proto_name}.proto
        DEPENDS ${proto_name}.proto ${IDL_OPTIONS_PROTO})
    target_sources(consume_${component} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.cc
        ${CMAKE_CURRENT_BINARY_DIR}/${proto_name}.pb.h)
    target_include_directories(consume_${component} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
# Mimic an application whose other dependencies also use these components.
target_link_libraries(consume_brpc PRIVATE brpc::bvar brpc::bthread brpc::butil)
set_tests_properties(consume_brpc PROPERTIES TIMEOUT 15)
