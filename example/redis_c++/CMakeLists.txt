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
project(redis_c++ C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

# Install dependencies:
# With apt:
# sudo apt-get install libreadline-dev 
# sudo apt-get install ncurses-dev
# With yum:
# sudo yum install readline-devel
# sudo yum install ncurses-devel

option(LINK_SO "Whether examples are linked dynamically" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

find_path(GPERFTOOLS_INCLUDE_DIR NAMES gperftools/heap-profiler.h)
find_library(GPERFTOOLS_LIBRARIES NAMES tcmalloc_and_profiler)
list(APPEND DYNAMIC_LIB ${GPERFTOOLS_LIBRARIES})

add_executable(redis_cli redis_cli.cpp)
brpc_example_configure_target(redis_cli)
add_executable(redis_press redis_press.cpp)
brpc_example_configure_target(redis_press)
add_executable(redis_server redis_server.cpp)
brpc_example_configure_target(redis_server)
add_executable(redis_cluster_client redis_cluster_client.cpp)
brpc_example_configure_target(redis_cluster_client)

set(AUX_LIB readline ncurses)

target_link_libraries(redis_cli PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB} ${AUX_LIB})
target_link_libraries(redis_press PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(redis_server PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(redis_cluster_client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
