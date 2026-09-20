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

find_package(Protobuf REQUIRED)
find_program(FLATC_EXECUTABLE flatc)
if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "Codegen tests need the official flatc executable")
endif()
get_filename_component(BRPC_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/include/butil")
# Keep test-only configuration in this build tree, never in the repository.
set(WITH_FLATBUFFERS_VAL 1)
configure_file("${BRPC_SOURCE_DIR}/config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/include/butil/config.h")
set(CODEGEN_INCLUDE_DIRS
    "${CMAKE_CURRENT_BINARY_DIR}/include"
    "${BRPC_SOURCE_DIR}/src"
    "${GENERATED_DIR}"
    "${FLATBUFFERS_INCLUDE_DIR}"
    ${Protobuf_INCLUDE_DIRS})
add_custom_command(
    OUTPUT "${GENERATED_DIR}/echo.brpc.fb.cpp"
           "${GENERATED_DIR}/echo.brpc.fb.h"
           "${GENERATED_DIR}/echo_generated.h"
    COMMAND "${FLATC_EXECUTABLE}" --cpp -o "${GENERATED_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs"
    COMMAND brpc_flatc -o "${GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs"
    DEPENDS brpc_flatc "${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs"
    VERBATIM)
add_library(brpc_codegen_compile OBJECT "${GENERATED_DIR}/echo.brpc.fb.cpp")
target_include_directories(brpc_codegen_compile PRIVATE ${CODEGEN_INCLUDE_DIRS})
target_compile_features(brpc_codegen_compile PRIVATE cxx_std_14)

add_test(NAME flatbuffers_codegen_acceptance
    COMMAND "${CMAKE_COMMAND}"
        "-DGENERATOR=$<TARGET_FILE:brpc_flatc>"
        "-DFLATC=${FLATC_EXECUTABLE}"
        "-DSCHEMA=${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/acceptance"
        "-DCXX=${CMAKE_CXX_COMPILER}"
        "-DINCLUDE_DIRS=${CODEGEN_INCLUDE_DIRS}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/acceptance.cmake")

set(BRPC_CODEGEN_BRPC_LIBRARY "" CACHE FILEPATH
    "Optional existing FlatBuffers-enabled libbrpc for runtime acceptance")
set(BRPC_CODEGEN_EXTRA_LIBRARIES "" CACHE STRING
    "Additional link dependencies of the supplied libbrpc")
if(BRPC_CODEGEN_BRPC_LIBRARY)
    find_package(Threads REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_library(CODEGEN_GFLAGS_LIBRARY gflags)
    find_library(CODEGEN_LEVELDB_LIBRARY leveldb)
    add_executable(brpc_codegen_runtime runtime.cpp $<TARGET_OBJECTS:brpc_codegen_compile>)
    target_include_directories(brpc_codegen_runtime PRIVATE ${CODEGEN_INCLUDE_DIRS})
    target_compile_features(brpc_codegen_runtime PRIVATE cxx_std_14)
    target_link_libraries(brpc_codegen_runtime PRIVATE
        "${BRPC_CODEGEN_BRPC_LIBRARY}" ${Protobuf_LIBRARIES}
        ${CODEGEN_GFLAGS_LIBRARY} ${CODEGEN_LEVELDB_LIBRARY}
        OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
        ${CMAKE_DL_LIBS} ${BRPC_CODEGEN_EXTRA_LIBRARIES})
    add_test(NAME flatbuffers_codegen_runtime COMMAND brpc_codegen_runtime)
endif()
