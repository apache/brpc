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
