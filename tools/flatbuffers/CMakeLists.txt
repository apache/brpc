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

cmake_minimum_required(VERSION 3.10)
project(brpc_flatc LANGUAGES CXX)

find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/idl.h)
find_library(FLATBUFFERS_LIBRARY NAMES flatbuffers)
if(NOT FLATBUFFERS_INCLUDE_DIR OR NOT FLATBUFFERS_LIBRARY)
    message(FATAL_ERROR "Install official FlatBuffers development headers and libflatbuffers")
endif()

add_executable(brpc_flatc brpc_flatc.cpp)
target_compile_features(brpc_flatc PRIVATE cxx_std_11)
target_include_directories(brpc_flatc SYSTEM PRIVATE "${FLATBUFFERS_INCLUDE_DIR}")
target_link_libraries(brpc_flatc PRIVATE "${FLATBUFFERS_LIBRARY}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(brpc_flatc PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../test/flatbuffers_codegen"
                     "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
