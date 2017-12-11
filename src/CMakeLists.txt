include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
file(GLOB PROTOS "*.proto")
list(APPEND PROTO_FLAGS -I${CMAKE_CURRENT_BINARY_DIR})
foreach(PROTO ${PROTOS})
    get_filename_component(PROTO_WE ${PROTO} NAME_WE)
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.pb.cc")
    execute_process(
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src ${PROTO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

file(GLOB brpc_PROTOS "brpc/*.proto")
foreach(PROTO ${brpc_PROTOS})
    get_filename_component(PROTO_WE ${PROTO} NAME_WE)
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/brpc/${PROTO_WE}.pb.cc")
    execute_process(
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/src/brpc/ ${PROTO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

file(GLOB brpc_policy_PROTOS "brpc/policy/*.proto")
foreach(PROTO ${brpc_policy_PROTOS})
    get_filename_component(PROTO_WE ${PROTO} NAME_WE)

    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/brpc/policy/${PROTO_WE}.pb.cc")

    execute_process(
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/src/brpc/policy ${PROTO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_SOURCE_DIR}/src)

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

aux_source_directory(${CMAKE_SOURCE_DIR}/src/bvar BVAR_SOURCES1)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/bvar/detail BVAR_SOURCES2)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/bthread BTHREAD_SOURCES)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/json2pb JSON2PB_SOURCES)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc BRPC_SOURCES1)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/details BRPC_SOURCES2)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/builtin BRPC_SOURCES3)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/policy BRPC_SOURCES4)

set(MCPACK2PB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/field_type.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/mcpack2pb.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/serializer.cpp
    )

set(SOURCES
    ${BUTIL_SOURCES}
    ${BVAR_SOURCES1}
    ${BVAR_SOURCES2}
    ${BTHREAD_SOURCES}
    ${JSON2PB_SOURCES}
    ${MCPACK2PB_SOURCES}
    ${BRPC_SOURCES1}
    ${BRPC_SOURCES2}
    ${BRPC_SOURCES3}
    ${BRPC_SOURCES4}
    ${PROTO_SRCS}
    )

add_library(brpc SHARED ${SOURCES})
add_library(brpc_static STATIC ${SOURCES})

target_link_libraries(brpc 
    #${CMAKE_THREAD_LIBS_INIT} 
    ${THREADS_LIB}
    #${GFLAGS_LIB} 
    ${GFLAGS_LIBRARY}
    ${PROTOBUF_LIB} 
    ${LEVELDB_LIB} 
    rt
    ssl
    crypto
    dl
    z
    ${PROTOC_LIB}
    )

if(WITH_GLOG)
    target_link_libraries(brpc ${GLOG_LIB})
endif()
    
set(protoc_gen_mcpack_SOURCES
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/generator.cpp
 )
add_executable(protoc-gen-mcpack ${protoc_gen_mcpack_SOURCES})
target_link_libraries(protoc-gen-mcpack brpc)
    
#install directory
# cmake -DCMAKE_INSTALL_PREFIX=/usr
install(TARGETS brpc
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        )

install(DIRECTORY src/
        DESTINATION include
        FILES_MATCHING 
        PATTERN "*.h*"
        PATTERN "*.proto"
        )
