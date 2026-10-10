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

set(WITH_DEBUG_LOCK_VAL "0")
if(WITH_DEBUG_LOCK)
    set(WITH_DEBUG_LOCK_VAL "1")
endif()

set(WITH_DEBUG_BTHREAD_SCHE_SAFETY_VAL "0")
if(WITH_DEBUG_BTHREAD_SCHE_SAFETY)
    set(WITH_DEBUG_BTHREAD_SCHE_SAFETY_VAL "1")
endif()

set(BTHREAD_DEFINITIONS
    BRPC_DEBUG_LOCK=${WITH_DEBUG_LOCK_VAL}
    BRPC_DEBUG_BTHREAD_SCHE_SAFETY=${WITH_DEBUG_BTHREAD_SCHE_SAFETY_VAL}
    BTHREAD_USE_FAST_PTHREAD_MUTEX)
set(BTHREAD_INCLUDE_DIRS)
if (WITH_BTHREAD_TRACER)
    if (NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux")  OR NOT (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64"))
        message(FATAL_ERROR "bthread tracer is only supported on Linux x86_64 platform")
    endif()
    find_path(LIBUNWIND_INCLUDE_PATH NAMES libunwind.h)
    find_library(LIBUNWIND_LIB NAMES unwind)
    find_library(LIBUNWIND_X86_64_LIB NAMES unwind-x86_64)
    if (NOT LIBUNWIND_INCLUDE_PATH OR NOT LIBUNWIND_LIB OR NOT LIBUNWIND_X86_64_LIB)
        message(FATAL_ERROR "Fail to find libunwind, which is needed by bthread tracer")
    endif()
    find_package(absl REQUIRED CONFIG)
    set(bthread_tracer_ABSL_USED_TARGETS absl::base absl::stacktrace absl::symbolize)
    list(APPEND BTHREAD_DEFINITIONS BRPC_BTHREAD_TRACER)
    list(APPEND BTHREAD_INCLUDE_DIRS ${LIBUNWIND_INCLUDE_PATH})
endif ()

add_library(bthread_build_config INTERFACE)
target_link_libraries(bthread_build_config INTERFACE butil_build_config)
target_compile_definitions(bthread_build_config INTERFACE ${BTHREAD_DEFINITIONS})
target_include_directories(bthread_build_config INTERFACE ${BTHREAD_INCLUDE_DIRS})

file(GLOB_RECURSE BTHREAD_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

set(bthread_libraries)
set(bthread_private_libs "")
if(WITH_BTHREAD_TRACER)
    list(APPEND bthread_libraries ${LIBUNWIND_LIB} ${LIBUNWIND_X86_64_LIB}
        ${bthread_tracer_ABSL_USED_TARGETS})
    set(bthread_private_libs "-lunwind -lunwind-x86_64 -labsl_stacktrace -labsl_symbolize -labsl_debugging_internal -labsl_demangle_internal -labsl_malloc_internal -labsl_raw_logging_internal -labsl_spinlock_wait -labsl_base")
endif()

add_library(bthread_runtime_dependencies INTERFACE)
target_link_libraries(bthread_runtime_dependencies INTERFACE ${bthread_libraries})

add_library(BTHREAD_LIB OBJECT ${BTHREAD_SOURCES})
target_link_libraries(BTHREAD_LIB PRIVATE bthread_build_config)
set_property(TARGET BTHREAD_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET BTHREAD_LIB PROPERTY BRPC_BUNDLE_LIBRARIES bthread_runtime_dependencies)
set_property(TARGET BTHREAD_LIB PROPERTY BRPC_PRIVATE_LIBS "${bthread_private_libs}")

if(BRPC_BUILD_COMPONENT_LIBS)
    if(WITH_BTHREAD_TRACER)
        foreach(lib unwind unwind-x86_64)
            string(REPLACE "-" "_" lib_var ${lib})
            find_library(BRPC_${lib_var}_LIBRARY NAMES ${lib})
            if(NOT BRPC_${lib_var}_LIBRARY)
                message(FATAL_ERROR "Fail to find ${lib}")
            endif()
            add_library(brpc::${lib_var} UNKNOWN IMPORTED)
            set_target_properties(brpc::${lib_var} PROPERTIES
                IMPORTED_LOCATION "${BRPC_${lib_var}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${LIBUNWIND_INCLUDE_PATH}")
        endforeach()
    endif()
    brpc_add_component(bthread BTHREAD_LIB)
    target_compile_definitions(bthread PUBLIC ${BTHREAD_DEFINITIONS})
    target_link_libraries(bthread PUBLIC brpc::bvar brpc::butil)
    if(WITH_BTHREAD_TRACER)
        target_link_libraries(bthread PUBLIC brpc::unwind brpc::unwind_x86_64
            ${bthread_tracer_ABSL_USED_TARGETS})
    endif()
endif()

brpc_install_module_headers()
