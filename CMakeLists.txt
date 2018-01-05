cmake_minimum_required(VERSION 2.8.10)
project(brpc C CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # require at least gcc 4.8
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # require at least clang 3.3
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang is too old, please install a newer version supporting C++11")
    endif()
else()
    message(WARNING "You are using an unsupported compiler! Compilation has only been tested with Clang and GCC.")
endif()

option(WITH_GLOG "With glog" OFF)
option(DEBUG "Print debug logs" OFF)
option(WITH_DEBUG_SYMBOLS "With debug symbols" OFF)
option(BUILD_EXAMPLE "Whether to build examples" OFF)
option(BUILD_UNIT_TESTS "Whether to build unit tests" OFF)
option(EXAMPLE_LINK_SO "Whether examples are linked dynamically" OFF)

set(WITH_GLOG_VAL "0")
if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
endif()

if(WITH_DEBUG_SYMBOLS)
    set(DEBUG_SYMBOL "-g")
endif()

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/src/butil/config.h @ONLY)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(GFLAGS REQUIRED)

execute_process(
    COMMAND bash -c "grep \"namespace [_A-Za-z0-9]\\+ {\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}' | tr -d '\n'"
    OUTPUT_VARIABLE GFLAGS_NS
)
if(${GFLAGS_NS} STREQUAL "GFLAGS_NAMESPACE")
    execute_process(
        COMMAND bash -c "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d '\n'"
        OUTPUT_VARIABLE GFLAGS_NS
    )
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/example
    ${CMAKE_CURRENT_BINARY_DIR}
)

execute_process(
    COMMAND bash -c "git rev-parse --short HEAD | tr -d '\n'"
    OUTPUT_VARIABLE BRPC_REVISION
)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DBRPC_REVISION=\\\"${BRPC_REVISION}\\\" -D__STRICT_ANSI__")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} ${DEBUG_SYMBOL}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    #required by butil/crc32.cc to boost performance for 10x
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
    endif()
    if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
    endif()
endif()

include(FindProtobuf)

find_path(LEVELDB_HEADER NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)

if(WITH_GLOG)
    find_path(GLOG_HEADER NAMES glog/logging.h)
    find_library(GLOG_LIB NAMES glog)
    include_directories(${GLOG_HEADER})
endif()

find_library(PROTOC_LIB NAMES protoc)

include_directories(
        ${GFLAGS_INCLUDE_PATH}
        ${PROTOBUF_INCLUDE_DIRS}
        ${LEVELDB_HEADER}
        )

set(DYNAMIC_LIB
    ${GFLAGS_LIBRARY}
    ${PROTOBUF_LIBRARIES}
    ${LEVELDB_LIB}
    ${PROTOC_LIB}
    rt
    ssl
    crypto
    dl
    z
    )

# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

file(GLOB PROTOS "${CMAKE_SOURCE_DIR}/src/*.proto")
list(APPEND PROTO_FLAGS -I${CMAKE_CURRENT_BINARY_DIR})
foreach(PROTO ${PROTOS})
    get_filename_component(PROTO_WE ${PROTO} NAME_WE)
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_WE}.pb.cc")
    execute_process(
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src ${PROTO}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

file(GLOB BRPC_PROTOS "${CMAKE_SOURCE_DIR}/src/brpc/*.proto")
file(GLOB BRPC_POLICY_PROTOS "${CMAKE_SOURCE_DIR}/src/brpc/policy/*.proto")

# list all source files
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

aux_source_directory(${CMAKE_SOURCE_DIR}/src/bvar BVAR_SOURCES)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/bvar/detail BVAR_DETAIL_SOURCES)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/bthread BTHREAD_SOURCES)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/json2pb JSON2PB_SOURCES)

aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc BRPC_SOURCES)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/details BRPC_DETAILS_SOURCES)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/builtin BRPC_BUILTIN_SOURCES)
aux_source_directory(${CMAKE_SOURCE_DIR}/src/brpc/policy BRPC_POLICY_SOURCES)

set(MCPACK2PB_SOURCES
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/field_type.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/mcpack2pb.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/parser.cpp
    ${CMAKE_SOURCE_DIR}/src/mcpack2pb/serializer.cpp
    )

set(SOURCES
    ${BVAR_SOURCES}
    ${BVAR_DETAIL_SOURCES}
    ${BTHREAD_SOURCES}
    ${JSON2PB_SOURCES}
    ${MCPACK2PB_SOURCES}
    ${BRPC_SOURCES}
    ${BRPC_DETAILS_SOURCES}
    ${BRPC_BUILTIN_SOURCES}
    ${BRPC_POLICY_SOURCES}
    ${PROTO_SRCS}
    ${BRPC_PROTOS}
    ${BRPC_POLICY_PROTOS}
    )

add_subdirectory(src)
if(BUILD_EXAMPLE)
    add_subdirectory(example)
endif()
if(BUILD_UNIT_TESTS)
    add_subdirectory(test)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/src/
        DESTINATION include
        FILES_MATCHING 
        PATTERN "*.h"
        PATTERN "*.hpp"
        )
