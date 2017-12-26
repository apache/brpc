include_directories(${CMAKE_CURRENT_BINARY_DIR})
file(GLOB PROTOS "*.proto")
list(APPEND PROTO_FLAGS -I${CMAKE_CURRENT_BINARY_DIR})
foreach(PROTO ${PROTOS})
    get_filename_component(PROTO_WE ${PROTO} NAME_WE)
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.pb.cc")
    execute_process(
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/test ${PROTO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

find_path(GTEST_HEADER NAMES gtest/gtest.h)
find_library(GTEST_LIB NAMES gtest)

set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DUNIT_TEST -Dprivate=public -Dprotected=public -DBVAR_NOT_LINK_DEFAULT_VARIABLES -include ${CMAKE_SOURCE_DIR}/test/sstream_workaround.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x")


SET(TEST_BUTIL_SOURCES
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
    ${CMAKE_SOURCE_DIR}/test/popen_unittest.cpp
    ${CMAKE_SOURCE_DIR}/test/butil_unittest_main.cpp
    ${CMAKE_SOURCE_DIR}/test/butil_unittest_main.cpp
	)
    
add_executable(test_butil ${TEST_BUTIL_SOURCES} ${PROTO_SRCS})
target_link_libraries(test_butil brpc ${GTEST_LIB})
