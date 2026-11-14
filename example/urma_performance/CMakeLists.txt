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
project(urma_performance C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

option(LINK_SO "Whether examples are linked dynamically" OFF)
option(BUILD_URMA_FAULT_INJECTOR "Build the process-local SEND failure test module" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

protobuf_generate_cpp(PROTO_SRC PROTO_HEADER test.proto)
set(BRPC_EXAMPLE_WITH_URMA ON)
find_path(URMA_INCLUDE_PATH NAMES urma_api.h
    HINTS ENV URMA_ROOT
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES ub/umdk/urma umdk/urma urma
                  src/urma/lib/urma/core/include)
if(NOT URMA_INCLUDE_PATH)
    message(FATAL_ERROR
        "Fail to find urma_api.h. Install UMDK headers or set URMA_ROOT.")
endif()
find_library(URMA_LIB NAMES urma
    HINTS ENV URMA_ROOT
    PATH_SUFFIXES lib lib64)
if(URMA_LIB)
    list(APPEND DYNAMIC_LIB ${URMA_LIB})
else()
    message(STATUS
        "liburma not found; using the URMA implementation linked into brpc")
endif()

add_executable(urma_performance_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(urma_performance_client)
add_executable(urma_performance_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(urma_performance_server)

target_link_libraries(urma_performance_client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(urma_performance_server PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})

add_executable(urma_recovery_client
    recovery_client.cpp ${PROTO_SRC} ${PROTO_HEADER})
brpc_example_configure_target(urma_recovery_client)
target_link_libraries(urma_recovery_client PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})

if(BUILD_URMA_FAULT_INJECTOR)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The URMA fault injector uses Linux LD_PRELOAD")
    endif()
    add_library(urma_send_failure_injector MODULE send_failure_injector.c)
    set_target_properties(urma_send_failure_injector PROPERTIES
                          C_STANDARD 11 C_STANDARD_REQUIRED ON)
    target_include_directories(urma_send_failure_injector PRIVATE ${URMA_INCLUDE_PATH})
    target_link_libraries(urma_send_failure_injector PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)
    include(CTest)
    if(BUILD_TESTING)
        add_library(urma_injector_test_provider SHARED test_send_failure_injector.c)
        target_compile_definitions(urma_injector_test_provider PRIVATE URMA_INJECTOR_TEST_PROVIDER)
        target_include_directories(urma_injector_test_provider PRIVATE ${URMA_INCLUDE_PATH})
        add_executable(urma_send_failure_injector_test test_send_failure_injector.c)
        target_include_directories(urma_send_failure_injector_test PRIVATE ${URMA_INCLUDE_PATH})
        target_link_libraries(urma_send_failure_injector_test PRIVATE urma_injector_test_provider)
        add_dependencies(urma_send_failure_injector_test urma_send_failure_injector)
        add_test(NAME urma_send_failure_injector_test
                 COMMAND ${CMAKE_COMMAND} -E env
                         "LD_PRELOAD=$<TARGET_FILE:urma_send_failure_injector>"
                         $<TARGET_FILE:urma_send_failure_injector_test>)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME urma_recovery_injection_runner_test
                     COMMAND ${Python3_EXECUTABLE}
                             ${CMAKE_CURRENT_SOURCE_DIR}/test_run_recovery_injection.py)
        endif()
    endif()
endif()
