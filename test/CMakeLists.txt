find_package(Gperftools)
include_directories(${GPERFTOOLS_INCLUDE_DIR})
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
find_library(GTEST_MAIN_LIB NAMES gtest_main)

set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DUNIT_TEST -Dprivate=public -Dprotected=public -DBVAR_NOT_LINK_DEFAULT_VARIABLES -include ${CMAKE_SOURCE_DIR}/test/sstream_workaround.h")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer")

file(COPY ${CMAKE_SOURCE_DIR}/test/cert1.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert2.key DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert1.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/cert2.crt DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/jsonout DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/test/run_tests.sh DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

SET(TEST_BUTIL_SOURCES
    #${CMAKE_SOURCE_DIR}/test/popen_unittest.cpp
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

set(BUTIL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/dmg_fp/g_fmt.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/dmg_fp/dtoa_wrapper.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/dynamic_annotations/dynamic_annotations.c
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/icu/icu_utf.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/superfasthash/superfasthash.c
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/modp_b64/modp_b64.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/nspr/prtime.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/symbolize/demangle.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/symbolize/symbolize.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/snappy/snappy-sinksource.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/snappy/snappy-stubs-internal.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/snappy/snappy.cc
    ${CMAKE_SOURCE_DIR}/src/butil/third_party/murmurhash3/murmurhash3.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/arena.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/at_exit.cc
    ${CMAKE_SOURCE_DIR}/src/butil/atomicops_internals_x86_gcc.cc
    ${CMAKE_SOURCE_DIR}/src/butil/base64.cc
    ${CMAKE_SOURCE_DIR}/src/butil/big_endian.cc
    ${CMAKE_SOURCE_DIR}/src/butil/cpu.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/alias.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/asan_invalid_access.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/crash_logging.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/debugger.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/debugger_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/dump_without_crashing.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/proc_maps_linux.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/stack_trace.cc
    ${CMAKE_SOURCE_DIR}/src/butil/debug/stack_trace_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/environment.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_enumerator.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_enumerator_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_path.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_path_constants.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/memory_mapped_file.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/memory_mapped_file_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/scoped_file.cc
    ${CMAKE_SOURCE_DIR}/src/butil/files/scoped_temp_dir.cc
    ${CMAKE_SOURCE_DIR}/src/butil/file_util.cc
    ${CMAKE_SOURCE_DIR}/src/butil/file_util_linux.cc
    ${CMAKE_SOURCE_DIR}/src/butil/file_util_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/guid.cc
    ${CMAKE_SOURCE_DIR}/src/butil/guid_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/hash.cc
    ${CMAKE_SOURCE_DIR}/src/butil/lazy_instance.cc
    ${CMAKE_SOURCE_DIR}/src/butil/location.cc
    ${CMAKE_SOURCE_DIR}/src/butil/md5.cc
    ${CMAKE_SOURCE_DIR}/src/butil/memory/aligned_memory.cc
    ${CMAKE_SOURCE_DIR}/src/butil/memory/ref_counted.cc
    ${CMAKE_SOURCE_DIR}/src/butil/memory/ref_counted_memory.cc
    ${CMAKE_SOURCE_DIR}/src/butil/memory/singleton.cc
    ${CMAKE_SOURCE_DIR}/src/butil/memory/weak_ptr.cc
    ${CMAKE_SOURCE_DIR}/src/butil/posix/file_descriptor_shuffle.cc
    ${CMAKE_SOURCE_DIR}/src/butil/posix/global_descriptors.cc
    ${CMAKE_SOURCE_DIR}/src/butil/rand_util.cc
    ${CMAKE_SOURCE_DIR}/src/butil/rand_util_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/fast_rand.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/safe_strerror_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/sha1_portable.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/latin1_string_conversions.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/nullable_string16.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/safe_sprintf.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string16.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string_number_conversions.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string_split.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string_piece.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string_util.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/string_util_constants.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/stringprintf.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/sys_string_conversions_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/utf_offset_string_conversions.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/utf_string_conversion_utils.cc
    ${CMAKE_SOURCE_DIR}/src/butil/strings/utf_string_conversions.cc
    ${CMAKE_SOURCE_DIR}/src/butil/synchronization/cancellation_flag.cc
    ${CMAKE_SOURCE_DIR}/src/butil/synchronization/condition_variable_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/synchronization/waitable_event_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/non_thread_safe_impl.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/platform_thread_linux.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/platform_thread_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/simple_thread.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_checker_impl.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_collision_warner.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_id_name_manager.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_local_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_local_storage.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_local_storage_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/thread_restrictions.cc
    ${CMAKE_SOURCE_DIR}/src/butil/threading/watchdog.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/clock.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/default_clock.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/default_tick_clock.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/tick_clock.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/time.cc
    ${CMAKE_SOURCE_DIR}/src/butil/time/time_posix.cc
    ${CMAKE_SOURCE_DIR}/src/butil/version.cc
    ${CMAKE_SOURCE_DIR}/src/butil/logging.cc
    ${CMAKE_SOURCE_DIR}/src/butil/class_name.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/errno.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/find_cstr.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/status.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/string_printf.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/thread_local.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/unix_socket.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/endpoint.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/fd_utility.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/files/temp_file.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/files/file_watcher.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/time.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/zero_copy_stream_as_streambuf.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/crc32c.cc
    ${CMAKE_SOURCE_DIR}/src/butil/containers/case_ignored_flat_map.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/iobuf.cpp
    ${CMAKE_SOURCE_DIR}/src/butil/popen.cpp
    )
    
set(BVAR_SOURCES1
    ${CMAKE_SOURCE_DIR}/src/bvar/collector.cpp
    ${CMAKE_SOURCE_DIR}/src/bvar/gflag.cpp
    ${CMAKE_SOURCE_DIR}/src/bvar/latency_recorder.cpp
    ${CMAKE_SOURCE_DIR}/src/bvar/variable.cpp
    )
aux_source_directory(${CMAKE_SOURCE_DIR}/src/bvar/detail BVAR_SOURCES2)

file(GLOB TEST_BVAR_SRCS "bvar_*_unittest.cpp")
add_executable(test_bvar ${BUTIL_SOURCES} ${BVAR_SOURCES1} ${BVAR_SOURCES2} ${TEST_BVAR_SRCS})
target_link_libraries(test_bvar
    ${PROTOBUF_LIBRARIES}
    ${GTEST_LIB}
    ${GPERFTOOLS_LIBRARIES}
    ${GFLAGS_LIBRARY}
    ${PROTOC_LIB}
    rt
    ssl
    crypto
    dl
    z
    )

add_library(PROTO_OBJ OBJECT ${PROTO_SRCS})
add_executable(test_butil ${TEST_BUTIL_SOURCES} $<TARGET_OBJECTS:PROTO_OBJ>)
target_link_libraries(test_butil brpc ${GTEST_LIB})

file(GLOB BTHREAD_UNITTESTS "bthread*unittest.cpp")
foreach(BTHREAD_UT ${BTHREAD_UNITTESTS})
    get_filename_component(BTHREAD_UT_WE ${BTHREAD_UT} NAME_WE)
    add_executable(${BTHREAD_UT_WE} ${BTHREAD_UT} $<TARGET_OBJECTS:PROTO_OBJ>)
    target_link_libraries(${BTHREAD_UT_WE} brpc ${GTEST_MAIN_LIB} ${GPERFTOOLS_LIBRARIES} ${GTEST_LIB})
endforeach()

file(GLOB BRPC_UNITTESTS "brpc_*_unittest.cpp")
foreach(BRPC_UT ${BRPC_UNITTESTS})
    get_filename_component(BRPC_UT_WE ${BRPC_UT} NAME_WE)
    add_executable(${BRPC_UT_WE} ${BRPC_UT} $<TARGET_OBJECTS:PROTO_OBJ>)
    target_link_libraries(${BRPC_UT_WE} brpc ${GTEST_MAIN_LIB} ${GPERFTOOLS_LIBRARIES} ${GTEST_LIB})
endforeach()
