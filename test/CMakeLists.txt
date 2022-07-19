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

find_package(Gperftools)
include_directories(${GPERFTOOLS_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(CompileProto)
set(TEST_PROTO_FILES addressbook1.proto
                     addressbook_encode_decode.proto
                     addressbook_map.proto
                     addressbook.proto
                     echo.proto
                     iobuf.proto
                     message.proto
                     repeated.proto
                     snappy_message.proto
                     v1.proto
                     v2.proto
                     grpc.proto
                     health_check.proto)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test/hdrs)
set(PROTOC_FLAGS ${PROTOC_FLAGS} -I${CMAKE_SOURCE_DIR}/src)
compile_proto(PROTO_HDRS PROTO_SRCS ${CMAKE_BINARY_DIR}/test
                                    ${CMAKE_BINARY_DIR}/test/hdrs
                                    ${CMAKE_SOURCE_DIR}/test
                                    "${TEST_PROTO_FILES}")
add_library(TEST_PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})

set(BRPC_SYSTEM_GTEST_SOURCE_DIR "" CACHE PATH "System googletest source directory.")

if(DOWNLOAD_GTEST)
    include(SetupGtest)
elseif(BRPC_SYSTEM_GTEST_SOURCE_DIR)
    add_subdirectory("${BRPC_SYSTEM_GTEST_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/system-googletest-build")
else()
    message(FATAL_ERROR "Googletest is not available")
endif()

set(CMAKE_CPP_FLAGS "${DEFINE_CLOCK_GETTIME} -DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DBRPC_WITH_RDMA=${WITH_RDMA_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__=__unused__ -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DUNIT_TEST -Dprivate=public -Dprotected=public -DBVAR_NOT_LINK_DEFAULT_VARIABLES -D__STRICT_ANSI__ -include ${PROJECT_SOURCE_DIR}/test/sstream_workaround.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -g -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer")
use_cxx11()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    #required by butil/crc32.cc to boost performance for 10x
    if((CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)") AND NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
    endif()
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
    endif()
endif()

file(COPY ${PROJECT_SOURCE_DIR}/test/cert1.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/test/cert2.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/test/cert1.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/test/cert2.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/test/jsonout DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${PROJECT_SOURCE_DIR}/test/run_tests.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

SET(TEST_BUTIL_SOURCES
    ${PROJECT_SOURCE_DIR}/test/recordio_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/popen_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/bounded_queue_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/at_exit_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/atomicops_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/base64_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/big_endian_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/bits_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/hash_tables_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/linked_list_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/mru_cache_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/small_map_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/stack_container_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/cpu_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/crash_logging_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/leak_tracker_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/stack_trace_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/environment_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/file_util_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/dir_reader_posix_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/file_path_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/file_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_temp_dir_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/guid_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/hash_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/lazy_instance_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/aligned_memory_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/linked_ptr_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/ref_counted_memory_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/ref_counted_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_ptr_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_vector_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/singleton_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/weak_ptr_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/observer_list_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/file_descriptor_shuffle_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/rand_util_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/safe_numerics_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_clear_errno_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_generic_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/security_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/sha1_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/stl_util_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/nullable_string16_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/safe_sprintf_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string16_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/stringprintf_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string_number_conversions_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string_piece_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string_split_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string_tokenizer_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/string_util_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/stringize_macros_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/sys_string_conversions_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/utf_offset_string_conversions_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/utf_string_conversions_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/cancellation_flag_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/condition_variable_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/lock_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/waitable_event_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/type_traits_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/non_thread_safe_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/platform_thread_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/simple_thread_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/thread_checker_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/thread_collision_warner_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/thread_id_name_manager_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/thread_local_storage_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/thread_local_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/watchdog_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/time_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/version_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/logging_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/cacheline_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/class_name_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/endpoint_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/unique_ptr_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/errno_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/fd_guard_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/file_watcher_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/find_cstr_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/scoped_lock_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/status_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/string_printf_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/string_splitter_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/synchronous_event_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/temp_file_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/baidu_thread_local_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/baidu_time_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/flat_map_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/crc32c_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/iobuf_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/object_pool_unittest.cpp
    ${PROJECT_SOURCE_DIR}/test/test_switches.cc
    ${PROJECT_SOURCE_DIR}/test/scoped_locale.cc
    ${PROJECT_SOURCE_DIR}/test/butil_unittest_main.cpp
    ${PROJECT_SOURCE_DIR}/test/butil_unittest_main.cpp
	)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
SET(TEST_BUTIL_SOURCES ${TEST_BUTIL_SOURCES}
    ${PROJECT_SOURCE_DIR}/test/proc_maps_linux_unittest.cc
    ${PROJECT_SOURCE_DIR}/test/test_file_util_linux.cc)
endif()

# bthread_* functions are used in logging.cc, and they need to be marked as
# weak symbols explicitly in Darwin system.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(DYNAMIC_LIB ${DYNAMIC_LIB}
                    "-Wl,-U,_bthread_getspecific"
                    "-Wl,-U,_bthread_setspecific"
                    "-Wl,-U,_bthread_key_create")
endif()

add_library(BUTIL_DEBUG_LIB OBJECT ${BUTIL_SOURCES})
add_library(SOURCES_DEBUG_LIB OBJECT ${SOURCES})
add_dependencies(SOURCES_DEBUG_LIB PROTO_LIB)

# shared library needs POSITION_INDEPENDENT_CODE
set_property(TARGET ${BUTIL_DEBUG_LIB} PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET ${SOURCES_DEBUG_LIB} PROPERTY POSITION_INDEPENDENT_CODE 1)

add_library(brpc-shared-debug SHARED $<TARGET_OBJECTS:BUTIL_DEBUG_LIB>
                                     $<TARGET_OBJECTS:SOURCES_DEBUG_LIB>
                                     $<TARGET_OBJECTS:PROTO_LIB>)
# change the debug lib output dir to be different from the release output
set_target_properties(brpc-shared-debug PROPERTIES
         LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/test)

target_link_libraries(brpc-shared-debug ${DYNAMIC_LIB})
if(BRPC_WITH_GLOG)
    target_link_libraries(brpc-shared-debug ${GLOG_LIB})
endif()

# test_butil
add_executable(test_butil ${TEST_BUTIL_SOURCES}
                          ${CMAKE_CURRENT_BINARY_DIR}/iobuf.pb.cc)
target_link_libraries(test_butil brpc-shared-debug
                                 gtest
                                 ${GPERFTOOLS_LIBRARIES})

add_test(NAME test_butil COMMAND test_butil)

# test_bvar
# -DBVAR_NOT_LINK_DEFAULT_VARIABLES not work for gcc >= 5.0, just remove the file to prevent linking into unit tests
list(REMOVE_ITEM BVAR_SOURCES ${PROJECT_SOURCE_DIR}/src/bvar/default_variables.cpp)

add_library(BVAR_DEBUG_LIB OBJECT ${BVAR_SOURCES})
file(GLOB TEST_BVAR_SRCS "bvar_*_unittest.cpp")
add_executable(test_bvar ${TEST_BVAR_SRCS}
                         $<TARGET_OBJECTS:BVAR_DEBUG_LIB>
                         $<TARGET_OBJECTS:BUTIL_DEBUG_LIB>)
target_link_libraries(test_bvar gtest
                                ${GPERFTOOLS_LIBRARIES}
                                ${DYNAMIC_LIB})
add_test(NAME test_bvar COMMAND test_bvar)

# bthread tests
file(GLOB BTHREAD_UNITTESTS "bthread*unittest.cpp")
foreach(BTHREAD_UT ${BTHREAD_UNITTESTS})
    get_filename_component(BTHREAD_UT_WE ${BTHREAD_UT} NAME_WE)
    add_executable(${BTHREAD_UT_WE} ${BTHREAD_UT})
    target_link_libraries(${BTHREAD_UT_WE} gtest_main
                                           brpc-shared-debug
                                           ${GPERFTOOLS_LIBRARIES})
    add_test(NAME ${BTHREAD_UT_WE} COMMAND ${BTHREAD_UT_WE})
endforeach()

# brpc tests
file(GLOB BRPC_UNITTESTS "brpc_*_unittest.cpp")
foreach(BRPC_UT ${BRPC_UNITTESTS})
    get_filename_component(BRPC_UT_WE ${BRPC_UT} NAME_WE)
    add_executable(${BRPC_UT_WE} ${BRPC_UT} $<TARGET_OBJECTS:TEST_PROTO_LIB>)
    target_link_libraries(${BRPC_UT_WE} brpc-shared-debug
                                        gtest_main
                                        ${GPERFTOOLS_LIBRARIES})
    add_test(NAME ${BRPC_UT_WE} COMMAND ${BRPC_UT_WE})
endforeach()
