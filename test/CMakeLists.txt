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
                     v2.proto)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test/hdrs)
set(PROTOC_FLAGS ${PROTOC_FLAGS} -I${CMAKE_SOURCE_DIR}/src)
compile_proto(PROTO_HDRS PROTO_SRCS ${CMAKE_BINARY_DIR}/test
                                    ${CMAKE_BINARY_DIR}/test/hdrs
                                    ${CMAKE_SOURCE_DIR}/test
                                    "${TEST_PROTO_FILES}")
add_library(TEST_PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})

option(BRPC_DOWNLOAD_GTEST "Download and build a fresh copy of googletest. Requires Internet access." ON)
set(BRPC_SYSTEM_GTEST_SOURCE_DIR "" CACHE PATH "System googletest source directory.")

if(BRPC_DOWNLOAD_GTEST)
    include(SetupGtest)
elseif(BRPC_SYSTEM_GTEST_SOURCE_DIR)
    add_subdirectory("${BRPC_SYSTEM_GTEST_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/system-googletest-build")
else()
    message(FATAL_ERROR "Googletest is not available")
endif()

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DUNIT_TEST -Dprivate=public -Dprotected=public -DBVAR_NOT_LINK_DEFAULT_VARIABLES -D__STRICT_ANSI__ -include ${CMAKE_SOURCE_DIR}/test/sstream_workaround.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer")
use_cxx11()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    #required by butil/crc32.cc to boost performance for 10x
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
    endif()
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
    endif()
endif()

file(COPY ${CMAKE_SOURCE_DIR}/test/cert1.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert2.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert1.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert2.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/jsonout DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/run_tests.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

SET(TEST_BUTIL_SOURCES
    ${CMAKE_SOURCE_DIR}/test/popen_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/at_exit_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/atomicops_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/base64_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/big_endian_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/bits_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/hash_tables_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/linked_list_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/mru_cache_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/small_map_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/stack_container_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/cpu_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/crash_logging_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/leak_tracker_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/proc_maps_linux_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/stack_trace_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/environment_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/file_util_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/dir_reader_posix_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/file_path_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/file_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_temp_dir_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/guid_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/hash_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/lazy_instance_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/md5_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/aligned_memory_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/linked_ptr_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/ref_counted_memory_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/ref_counted_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_ptr_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_vector_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/singleton_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/weak_ptr_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/observer_list_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/file_descriptor_shuffle_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/rand_util_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/safe_numerics_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_clear_errno_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_generic_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/security_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/sha1_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/stl_util_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/nullable_string16_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/safe_sprintf_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string16_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/stringprintf_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string_number_conversions_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string_piece_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string_split_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string_tokenizer_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/string_util_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/stringize_macros_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/sys_string_conversions_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/utf_offset_string_conversions_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/utf_string_conversions_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/cancellation_flag_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/condition_variable_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/lock_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/waitable_event_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/type_traits_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/non_thread_safe_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/platform_thread_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/simple_thread_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/thread_checker_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/thread_collision_warner_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/thread_id_name_manager_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/thread_local_storage_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/thread_local_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/watchdog_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/pr_time_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/time_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/version_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/logging_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/cacheline_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/class_name_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/endpoint_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/unique_ptr_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/errno_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/fd_guard_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/file_watcher_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/find_cstr_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/scoped_lock_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/status_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/string_printf_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/string_splitter_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/synchronous_event_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/temp_file_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/baidu_thread_local_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/baidu_time_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/flat_map_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/crc32c_unittest.cc
    ${CMAKE_SOURCE_DIR}/test/iobuf_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/test_switches.cc
    ${CMAKE_SOURCE_DIR}/test/scoped_locale.cc
    ${CMAKE_SOURCE_DIR}/test/test_file_util_linux.cc
    ${CMAKE_SOURCE_DIR}/test/butil_unittest_main.cpp
    ${CMAKE_SOURCE_DIR}/test/butil_unittest_main.cpp
	)

# -DBVAR_NOT_LINK_DEFAULT_VARIABLES not work for gcc >= 5.0, just remove the file to prevent linking into unit tests
list(REMOVE_ITEM BVAR_SOURCES ${CMAKE_SOURCE_DIR}/src/bvar/default_variables.cpp)

file(GLOB TEST_BVAR_SRCS "bvar_*_unittest.cpp")
add_executable(test_bvar $<TARGET_OBJECTS:BUTIL_LIB>
                         $<TARGET_OBJECTS:PROTO_LIB>
                         ${BVAR_SOURCES}
                         ${TEST_BVAR_SRCS})
target_link_libraries(test_bvar gtest
                                ${GPERFTOOLS_LIBRARIES}
                                ${DYNAMIC_LIB})

add_executable(test_butil ${TEST_BUTIL_SOURCES}
                          $<TARGET_OBJECTS:TEST_PROTO_LIB>
                          $<TARGET_OBJECTS:BUTIL_LIB>
                          $<TARGET_OBJECTS:OBJ_LIB>
                          $<TARGET_OBJECTS:PROTO_LIB>)
target_link_libraries(test_butil gtest ${GPERFTOOLS_LIBRARIES} ${DYNAMIC_LIB})

file(GLOB BTHREAD_UNITTESTS "bthread*unittest.cpp")
foreach(BTHREAD_UT ${BTHREAD_UNITTESTS})
    get_filename_component(BTHREAD_UT_WE ${BTHREAD_UT} NAME_WE)
    add_executable(${BTHREAD_UT_WE} ${BTHREAD_UT} 
                                    $<TARGET_OBJECTS:TEST_PROTO_LIB>
                                    $<TARGET_OBJECTS:BUTIL_LIB>
                                    $<TARGET_OBJECTS:OBJ_LIB>
                                    $<TARGET_OBJECTS:PROTO_LIB>)
    target_link_libraries(${BTHREAD_UT_WE}
                          gtest_main
                          ${GPERFTOOLS_LIBRARIES}
                          ${DYNAMIC_LIB})
endforeach()

file(GLOB BRPC_UNITTESTS "brpc_*_unittest.cpp")
foreach(BRPC_UT ${BRPC_UNITTESTS})
    get_filename_component(BRPC_UT_WE ${BRPC_UT} NAME_WE)
    add_executable(${BRPC_UT_WE} ${BRPC_UT}
                                 $<TARGET_OBJECTS:TEST_PROTO_LIB>
                                 $<TARGET_OBJECTS:BUTIL_LIB>
                                 $<TARGET_OBJECTS:OBJ_LIB>
                                 $<TARGET_OBJECTS:PROTO_LIB>)
    target_link_libraries(${BRPC_UT_WE}
                          gtest_main
                          ${GPERFTOOLS_LIBRARIES}
                          ${GTEST_LIB}
                          ${DYNAMIC_LIB})
endforeach()
