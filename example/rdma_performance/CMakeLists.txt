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
project(rdma_performance C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

option(LINK_SO "Whether examples are linked dynamically" OFF)
option(WITH_GDR "Build with GPU Direct RDMA support (implies RDMA)" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

protobuf_generate_cpp(PROTO_SRC PROTO_HEADER test.proto)
set(BRPC_EXAMPLE_WITH_RDMA ON)
find_path(RDMA_INCLUDE_PATH NAMES infiniband/verbs.h)
find_library(RDMA_LIB NAMES ibverbs)
if ((NOT RDMA_INCLUDE_PATH) OR (NOT RDMA_LIB))
    message(FATAL_ERROR "Fail to find ibverbs")
endif()
list(APPEND DYNAMIC_LIB ${RDMA_LIB})

if(WITH_GDR)
    # GDR is a special mode of RDMA and requires CUDA.
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "GDR (GPU Direct RDMA) is only supported on Linux")
    endif()
    if(DEFINED ENV{CUDA_HOME} AND NOT CUDA_HOME)
        set(CUDA_HOME $ENV{CUDA_HOME})
    endif()
    if(CUDA_HOME)
        find_path(CUDA_INCLUDE_PATH NAMES cuda_runtime.h
                  PATHS ${CUDA_HOME}/include NO_DEFAULT_PATH)
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
        message(FATAL_ERROR "Fail to find CUDA (set CUDA_HOME or install CUDA toolkit)")
    endif()
    list(APPEND DYNAMIC_LIB ${CUDA_LIB} ${CUDART_LIB})
endif()

add_executable(client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(client)
add_executable(server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(server)

if(WITH_GDR)
    foreach(t client server)
        target_compile_definitions(${t} PRIVATE BRPC_WITH_GDR=1 BRPC_WITH_RDMA=1)
        target_include_directories(${t} PRIVATE ${CUDA_INCLUDE_PATH})
    endforeach()
endif()

target_link_libraries(client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(server PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
