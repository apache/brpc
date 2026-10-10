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

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc
                          ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.h
    COMMAND protobuf::protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        -I${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/echo.proto
    DEPENDS echo.proto)
target_sources(consume_brpc PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc
    ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.h)
target_include_directories(consume_brpc PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# Mimic an application whose other dependencies also use these components.
target_link_libraries(consume_brpc PRIVATE brpc::bvar brpc::bthread brpc::butil)
set_tests_properties(consume_brpc PROPERTIES TIMEOUT 15)
