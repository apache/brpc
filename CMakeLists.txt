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
project(brpc C CXX)

option(WITH_GLOG "With glog" OFF)
option(WITH_MESALINK "With MesaLink" OFF)
option(WITH_BORINGSSL "With BoringSSL" OFF)
option(WITH_DEBUG_SYMBOLS "With debug symbols" ON)
option(WITH_THRIFT "With thrift framed protocol supported" OFF)
option(WITH_FLATBUFFERS "With FlatBuffers message support (headers only)" OFF)
option(WITH_BTHREAD_TRACER "With bthread tracer supported" OFF)
option(WITH_SNAPPY "With snappy" OFF)
option(WITH_RDMA "With RDMA" OFF)
option(WITH_URMA "With URMA (openEuler Unified Remote Memory Access)" OFF)
set(URMA_USE_MOCK "AUTO" CACHE STRING
    "URMA mock mode: AUTO, ON, or OFF (OFF requires liburma)")
set_property(CACHE URMA_USE_MOCK PROPERTY STRINGS AUTO ON OFF)
option(DOWNLOAD_URMA_HEADERS
       "Download UMDK headers when WITH_URMA is enabled and headers are absent"
       ON)
option(WITH_GDR "With GPU Direct RDMA (requires WITH_RDMA and CUDA)" OFF)
option(WITH_UBRING "With UB" OFF)
option(WITH_DEBUG_BTHREAD_SCHE_SAFETY "With debugging bthread sche safety" OFF)
option(WITH_DEBUG_LOCK "With debugging lock" OFF)
option(WITH_CPU_FREQUENCY "Use CPU frequency for cpuwide_time" OFF)
option(WITH_ASAN "With AddressSanitizer" OFF)
option(BUILD_UNIT_TESTS "Whether to build unit tests" OFF)
option(BUILD_FUZZ_TESTS "Whether to build fuzz tests" OFF)
option(BUILD_BRPC_TOOLS "Whether to build brpc tools" ON)
option(DOWNLOAD_GTEST "Download and build a fresh copy of googletest. Requires Internet access." ON)

# Enable MACOSX_RPATH. Run "cmake --help-policy CMP0042" for policy details.
if(POLICY CMP0042)
    cmake_policy(SET CMP0042 NEW)
endif()

set(BRPC_VERSION 1.18.0)

SET(CPACK_GENERATOR "DEB")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "brpc authors")
INCLUDE(CPack)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # require at least gcc 5.0
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.0)
        message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++14")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # require at least clang 3.5
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.5)
        message(FATAL_ERROR "Clang is too old, please install a newer version supporting C++14")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif()

set(BRPC_COMMON_COMPILE_OPTIONS)
set(BRPC_COMMON_DEFINITIONS)
set(BRPC_COMMON_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set(BRPC_COMMON_LINK_OPTIONS)
set(BRPC_CXX_STANDARD 14)

if(WITH_DEBUG_SYMBOLS)
    list(APPEND BRPC_COMMON_COMPILE_OPTIONS -g)
endif()

set(WITH_GDR_VAL "0")
if(WITH_GDR)
    # GDR is a special mode of RDMA; it implies RDMA.
    if(NOT WITH_RDMA)
        message(STATUS "WITH_GDR=ON implies WITH_RDMA=ON")
        set(WITH_RDMA ON)
    endif()
    set(WITH_GDR_VAL "1")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "GDR (GPU Direct RDMA) is only supported on Linux")
    endif()
    message(STATUS "brpc compile with gdr")
    # Honor CUDA_HOME if set by the user, otherwise fall back to common defaults.
    if(DEFINED ENV{CUDA_HOME} AND NOT CUDA_HOME)
        set(CUDA_HOME $ENV{CUDA_HOME})
    endif()
    if(CUDA_HOME)
        set(CUDA_INCLUDE_PATH ${CUDA_HOME}/include)
        find_library(CUDA_LIB NAMES cuda PATHS ${CUDA_HOME}/lib64 ${CUDA_HOME}/lib NO_DEFAULT_PATH)
        find_library(CUDART_LIB NAMES cudart PATHS ${CUDA_HOME}/lib64 ${CUDA_HOME}/lib NO_DEFAULT_PATH)
    else()
        find_path(CUDA_INCLUDE_PATH NAMES cuda_runtime.h
                  PATHS /usr/local/cuda/include /usr/include)
        find_library(CUDA_LIB NAMES cuda
                     PATHS /usr/local/cuda/lib64 /usr/lib64)
        find_library(CUDART_LIB NAMES cudart
                     PATHS /usr/local/cuda/lib64 /usr/lib64)
    endif()
    if((NOT CUDA_INCLUDE_PATH) OR (NOT CUDA_LIB) OR (NOT CUDART_LIB))
        message(FATAL_ERROR "Fail to find CUDA (set CUDA_HOME or install CUDA toolkit). "
                            "Needed headers: cuda_runtime.h; libs: cuda, cudart")
    endif()
    list(APPEND BRPC_COMMON_INCLUDE_DIRS ${CUDA_INCLUDE_PATH})
endif()

include(GNUInstallDirs)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bRPCHeaders.cmake)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(GFLAGS REQUIRED)

execute_process(
    COMMAND bash -c "${CMAKE_CURRENT_SOURCE_DIR}/tools/get_brpc_revision.sh ${CMAKE_CURRENT_SOURCE_DIR} | tr -d '\n'"
    OUTPUT_VARIABLE BRPC_REVISION
)
string(REPLACE "\\|" "|" BRPC_REVISION "${BRPC_REVISION}")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND BRPC_COMMON_COMPILE_OPTIONS
        -Wno-deprecated-declarations
        -Wno-inconsistent-missing-override
    )
endif()

list(APPEND BRPC_COMMON_DEFINITIONS
    __const__=__unused__
    _GNU_SOURCE
    USE_SYMBOLIZE
    NO_TCMALLOC
    __STDC_FORMAT_MACROS
    __STDC_LIMIT_MACROS
    __STDC_CONSTANT_MACROS
)

if(WITH_GDR)
    list(APPEND BRPC_COMMON_DEFINITIONS BRPC_WITH_GDR=${WITH_GDR_VAL})
endif()

if(WITH_ASAN)
    list(APPEND BRPC_COMMON_COMPILE_OPTIONS -fsanitize=address)
    list(APPEND BRPC_COMMON_LINK_OPTIONS -fsanitize=address)
endif()

list(APPEND BRPC_COMMON_COMPILE_OPTIONS
    -O2
    -pipe
    -Wall
    -W
    -fPIC
    -fstrict-aliasing
    -Wno-unused-parameter
    -fno-omit-frame-pointer
    "-DBRPC_REVISION=\"${BRPC_REVISION}\""
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-invalid-offsetof>
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        # Preserve the libcontext workaround until its scope is verified.
        list(APPEND BRPC_COMMON_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-fno-gcse>)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        option(WITH_RISCV_ZBC "Enable RISC-V Zbc carry-less multiplication for CRC32C acceleration" OFF)
        option(WITH_RISCV_ZVBC "Enable RISC-V Zvbc vector carry-less multiplication for CRC32C acceleration" OFF)
        # The ISA controls RVV code paths throughout the project, not just CRC32C.
        if(WITH_RISCV_ZVBC)
            list(APPEND BRPC_COMMON_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-march=rv64gcv_zbc_zvbc>)
        elseif(WITH_RISCV_ZBC)
            list(APPEND BRPC_COMMON_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-march=rv64gc_zbc>)
        else()
            list(APPEND BRPC_COMMON_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-march=rv64gc>)
        endif()
    endif()
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
        list(APPEND BRPC_COMMON_COMPILE_OPTIONS $<$<COMPILE_LANGUAGE:CXX>:-Wno-aligned-new>)
    endif()
endif()

find_package(Protobuf REQUIRED)
foreach(protobuf_include_dir IN LISTS Protobuf_INCLUDE_DIRS)
    list(APPEND PROTOC_FLAGS -I${protobuf_include_dir})
endforeach()
if(Protobuf_VERSION VERSION_GREATER 4.21)
    # required by absl
    set(BRPC_CXX_STANDARD 17)

    find_package(absl REQUIRED CONFIG)
    set(protobuf_ABSL_USED_TARGETS
        absl::absl_check
        absl::absl_log
        absl::algorithm
        absl::base
        absl::bind_front
        absl::bits
        absl::btree
        absl::cleanup
        absl::cord
        absl::core_headers
        absl::debugging
        absl::die_if_null
        absl::dynamic_annotations
        absl::flags
        absl::flat_hash_map
        absl::flat_hash_set
        absl::function_ref
        absl::hash
        absl::layout
        absl::log_initialize
        absl::log_globals
        absl::log_severity
        absl::memory
        absl::node_hash_map
        absl::node_hash_set
        absl::random_distributions
        absl::random_random
        absl::span
        absl::status
        absl::statusor
        absl::strings
        absl::synchronization
        absl::time
        absl::type_traits
        absl::utility
        absl::variant
    )
endif()
find_package(Threads REQUIRED)

list(REMOVE_DUPLICATES BRPC_COMMON_INCLUDE_DIRS)

add_library(brpc_common_config INTERFACE)
target_include_directories(brpc_common_config INTERFACE ${BRPC_COMMON_INCLUDE_DIRS})
target_compile_definitions(brpc_common_config INTERFACE ${BRPC_COMMON_DEFINITIONS})
target_compile_options(brpc_common_config INTERFACE ${BRPC_COMMON_COMPILE_OPTIONS})
target_compile_features(brpc_common_config INTERFACE cxx_std_${BRPC_CXX_STANDARD})
if(BRPC_COMMON_LINK_OPTIONS)
    target_link_options(brpc_common_config INTERFACE ${BRPC_COMMON_LINK_OPTIONS})
endif()

# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/output/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/output/lib)

add_subdirectory(src)
add_subdirectory(test/cmake_link_dependencies EXCLUDE_FROM_ALL)
if(BUILD_UNIT_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(BUILD_FUZZ_TESTS AND NOT BUILD_UNIT_TESTS)
    message(FATAL_ERROR "BUILD_UNIT_TESTS must be enabled to build fuzz tests")
endif()

if(BUILD_BRPC_TOOLS)
    add_subdirectory(tools)
endif()

# Install pkgconfig
configure_file(cmake/brpc.pc.in ${PROJECT_BINARY_DIR}/brpc.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/brpc.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
