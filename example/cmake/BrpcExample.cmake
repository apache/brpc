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

include_guard(GLOBAL)

function(brpc_example_append_existing_dirs out_var)
    set(_dirs)
    foreach(_dir IN LISTS ARGN)
        if(_dir AND NOT _dir MATCHES "-NOTFOUND$")
            list(APPEND _dirs ${_dir})
        endif()
    endforeach()
    set(${out_var} ${_dirs} PARENT_SCOPE)
endfunction()

macro(brpc_example_find_common_deps out_libs)
    execute_process(
        COMMAND bash -c "find ${PROJECT_SOURCE_DIR}/../.. -type d -regex \".*output/include$\" | head -n1 | xargs dirname | tr -d '\n'"
        OUTPUT_VARIABLE OUTPUT_PATH
    )

    if(OUTPUT_PATH)
        list(PREPEND CMAKE_PREFIX_PATH ${OUTPUT_PATH})
    endif()

    find_package(Threads REQUIRED)
    find_package(Protobuf REQUIRED)

    # Search for libthrift* by best effort. If it is not found and brpc is
    # compiled with thrift protocol enabled, a link error would be reported.
    find_library(THRIFT_LIB NAMES thrift)
    if(NOT THRIFT_LIB)
        set(THRIFT_LIB "")
    endif()

    find_path(BRPC_INCLUDE_PATH NAMES brpc/server.h)
    if(LINK_SO)
        find_library(BRPC_LIB NAMES brpc)
    else()
        find_library(BRPC_LIB NAMES libbrpc.a brpc)
    endif()
    if((NOT BRPC_INCLUDE_PATH) OR (NOT BRPC_LIB))
        message(FATAL_ERROR "Fail to find brpc")
    endif()

    find_path(GFLAGS_INCLUDE_PATH gflags/gflags.h)
    find_library(GFLAGS_LIBRARY NAMES gflags libgflags)
    if((NOT GFLAGS_INCLUDE_PATH) OR (NOT GFLAGS_LIBRARY))
        message(FATAL_ERROR "Fail to find gflags")
    endif()

    find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
    find_library(LEVELDB_LIB NAMES leveldb)
    if((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
        message(FATAL_ERROR "Fail to find leveldb")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT OPENSSL_ROOT_DIR)
        set(OPENSSL_ROOT_DIR
            "/usr/local/opt/openssl"    # Homebrew installed OpenSSL
        )
    endif()

    find_package(OpenSSL REQUIRED)

    set(_common_libs
        Threads::Threads
        ${GFLAGS_LIBRARY}
        ${PROTOBUF_LIBRARIES}
        ${LEVELDB_LIB}
        ${OPENSSL_CRYPTO_LIBRARY}
        ${OPENSSL_SSL_LIBRARY}
        ${THRIFT_LIB}
        dl
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(_common_libs ${_common_libs}
            pthread
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreData"
            "-framework CoreText"
            "-framework Security"
            "-framework Foundation"
            "-Wl,-U,_MallocExtension_ReleaseFreeMemory"
            "-Wl,-U,_ProfilerStart"
            "-Wl,-U,_ProfilerStop"
            "-Wl,-U,__Z13GetStackTracePPvii"
            "-Wl,-U,_mallctl"
            "-Wl,-U,_malloc_stats_print"
        )
    endif()

    set(${out_libs} ${_common_libs})
endmacro()

function(brpc_example_configure_target target_name)
    brpc_example_append_existing_dirs(_include_dirs
        ${CMAKE_CURRENT_BINARY_DIR}
        ${BRPC_INCLUDE_PATH}
        ${GFLAGS_INCLUDE_PATH}
        ${LEVELDB_INCLUDE_PATH}
        ${OPENSSL_INCLUDE_DIR}
        ${GPERFTOOLS_INCLUDE_DIR}
        ${RDMA_INCLUDE_PATH}
    )

    if(_include_dirs)
        target_include_directories(${target_name} PRIVATE ${_include_dirs})
    endif()

    target_compile_features(${target_name} PRIVATE cxx_std_11)
    target_compile_definitions(${target_name} PRIVATE
        NDEBUG
        __const__=__unused__
    )
    target_compile_options(${target_name} PRIVATE
        -O2
        -pipe
        -W
        -Wall
        -Wno-unused-parameter
        -fPIC
        -fno-omit-frame-pointer
    )

    if(BRPC_EXAMPLE_ENABLE_CPU_PROFILER)
        target_compile_definitions(${target_name} PRIVATE BRPC_ENABLE_CPU_PROFILER)
    endif()

    if(BRPC_EXAMPLE_WITH_RDMA)
        target_compile_definitions(${target_name} PRIVATE BRPC_WITH_RDMA=1)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        include(CheckFunctionExists)
        check_function_exists(clock_gettime BRPC_EXAMPLE_HAVE_CLOCK_GETTIME)
        if(NOT BRPC_EXAMPLE_HAVE_CLOCK_GETTIME)
            target_compile_definitions(${target_name} PRIVATE NO_CLOCK_GETTIME_IN_MAC)
        endif()
    endif()
endfunction()
