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

set(protobuf_MODULE_COMPATIBLE ON CACHE BOOL "Expose Protobuf compatibility variables for codegen tests" FORCE)
find_package(Protobuf CONFIG QUIET)
set(CODEGEN_PROTOBUF_CONFIG ${Protobuf_FOUND})
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
set(CODEGEN_CXX_STANDARD 14)
if(Protobuf_VERSION VERSION_GREATER 4.21)
    set(CODEGEN_CXX_STANDARD 17)
    if(NOT CODEGEN_PROTOBUF_CONFIG)
        message(FATAL_ERROR "Protobuf 5+ needs its CMake config package for codegen tests")
    endif()
endif()
message(STATUS "Codegen tests: Protobuf ${Protobuf_VERSION}, C++${CODEGEN_CXX_STANDARD}")
find_program(FLATC_EXECUTABLE flatc)
if(NOT FLATC_EXECUTABLE)
    message(FATAL_ERROR "Codegen tests need the official flatc executable")
endif()
execute_process(COMMAND "${FLATC_EXECUTABLE}" --version
    OUTPUT_VARIABLE CODEGEN_FLATC_VERSION RESULT_VARIABLE CODEGEN_FLATC_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT CODEGEN_FLATC_RESULT EQUAL 0 OR
   NOT CODEGEN_FLATC_VERSION STREQUAL "flatc version 25.2.10")
    message(FATAL_ERROR "Codegen tests require official flatc 25.2.10: ${CODEGEN_FLATC_VERSION}")
endif()
find_path(CODEGEN_GFLAGS_INCLUDE_DIR gflags/gflags_declare.h)
if(NOT CODEGEN_GFLAGS_INCLUDE_DIR)
    message(FATAL_ERROR "Codegen tests need gflags development headers")
endif()
# execute_process does not inherit a target's compiler flags or macOS SDK.
set(CODEGEN_CXX_FLAGS "${CMAKE_CXX_COMPILER_ARG1} ${CMAKE_CXX_FLAGS}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    string(APPEND CODEGEN_CXX_FLAGS
        " $<$<CONFIG:${config}>:${CMAKE_CXX_FLAGS_${config}}>")
endforeach()
if(CMAKE_SYSROOT)
    string(APPEND CODEGEN_CXX_FLAGS " --sysroot=\"${CMAKE_SYSROOT}\"")
endif()
if(APPLE)
    if(CMAKE_OSX_SYSROOT)
        set(CODEGEN_OSX_SYSROOT "${CMAKE_OSX_SYSROOT}")
        if(NOT IS_ABSOLUTE "${CODEGEN_OSX_SYSROOT}")
            execute_process(COMMAND xcrun --sdk "${CMAKE_OSX_SYSROOT}" --show-sdk-path
                OUTPUT_VARIABLE CODEGEN_OSX_SYSROOT RESULT_VARIABLE sdk_result
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT sdk_result EQUAL 0 OR NOT IS_DIRECTORY "${CODEGEN_OSX_SYSROOT}")
                message(FATAL_ERROR "Cannot resolve codegen SDK: ${CMAKE_OSX_SYSROOT}")
            endif()
        endif()
        string(APPEND CODEGEN_CXX_FLAGS " -isysroot \"${CODEGEN_OSX_SYSROOT}\"")
    endif()
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        string(APPEND CODEGEN_CXX_FLAGS " -arch ${arch}")
    endforeach()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        string(APPEND CODEGEN_CXX_FLAGS
            " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
endif()
message(STATUS "Codegen flatc: ${FLATC_EXECUTABLE} (${CODEGEN_FLATC_VERSION})")
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
    "${CODEGEN_GFLAGS_INCLUDE_DIR}"
    ${Protobuf_INCLUDE_DIRS})
get_target_property(CODEGEN_PROTOBUF_INCLUDE_DIRS protobuf::libprotobuf INTERFACE_INCLUDE_DIRECTORIES)
if(CODEGEN_PROTOBUF_INCLUDE_DIRS)
    list(APPEND CODEGEN_INCLUDE_DIRS ${CODEGEN_PROTOBUF_INCLUDE_DIRS})
endif()
if(TARGET absl::base)
    get_target_property(CODEGEN_ABSL_INCLUDE_DIRS absl::base INTERFACE_INCLUDE_DIRECTORIES)
    if(CODEGEN_ABSL_INCLUDE_DIRS)
        list(APPEND CODEGEN_INCLUDE_DIRS ${CODEGEN_ABSL_INCLUDE_DIRS})
    endif()
endif()
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
target_compile_features(brpc_codegen_compile PRIVATE cxx_std_${CODEGEN_CXX_STANDARD})

add_test(NAME flatbuffers_codegen_acceptance
    COMMAND "${CMAKE_COMMAND}"
        "-DGENERATOR=$<TARGET_FILE:brpc_flatc_test>"
        "-DFLATC=${FLATC_EXECUTABLE}"
        "-DSCHEMA=${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/acceptance"
        "-DCXX=${CMAKE_CXX_COMPILER}"
        "-DCXX_FLAGS=${CODEGEN_CXX_FLAGS}"
        "-DCXX_ID=${CMAKE_CXX_COMPILER_ID}"
        "-DIMPLICIT_INCLUDE_DIRS=${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}"
        "-DCXX_STANDARD=${CODEGEN_CXX_STANDARD}"
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
    target_compile_features(brpc_codegen_runtime PRIVATE cxx_std_${CODEGEN_CXX_STANDARD})
    target_link_libraries(brpc_codegen_runtime PRIVATE
        "${BRPC_CODEGEN_BRPC_LIBRARY}" protobuf::libprotobuf
        ${CODEGEN_GFLAGS_LIBRARY} ${CODEGEN_LEVELDB_LIBRARY}
        OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads
        ${CMAKE_DL_LIBS} ${BRPC_CODEGEN_EXTRA_LIBRARIES})
    add_test(NAME flatbuffers_codegen_runtime COMMAND brpc_codegen_runtime)
endif()
