# Usage:
# export PROTOBUF_HOME=/usr/local/protobuf3
# python compile_proto.py
# mkdir build 
# cd build 
# cmake ..
#
# Written by Kevin.XU @ 2017.11.16


cmake_minimum_required(VERSION 3.0)
project(brpc)

set(TOPDIR .)

include_directories(
        ${TOPDIR}/src
        ${TOPDIR}/example
)

set(BUTIL_SOURCES
    ${TOPDIR}/src/butil/third_party/dmg_fp/g_fmt.cc
    ${TOPDIR}/src/butil/third_party/dmg_fp/dtoa_wrapper.cc
    ${TOPDIR}/src/butil/third_party/dynamic_annotations/dynamic_annotations.c
    ${TOPDIR}/src/butil/third_party/icu/icu_utf.cc
    ${TOPDIR}/src/butil/third_party/superfasthash/superfasthash.c
    ${TOPDIR}/src/butil/third_party/modp_b64/modp_b64.cc
    ${TOPDIR}/src/butil/third_party/nspr/prtime.cc
    ${TOPDIR}/src/butil/third_party/symbolize/demangle.cc
    ${TOPDIR}/src/butil/third_party/symbolize/symbolize.cc
    ${TOPDIR}/src/butil/third_party/snappy/snappy-sinksource.cc
    ${TOPDIR}/src/butil/third_party/snappy/snappy-stubs-internal.cc
    ${TOPDIR}/src/butil/third_party/snappy/snappy.cc
    ${TOPDIR}/src/butil/third_party/murmurhash3/murmurhash3.cpp
    ${TOPDIR}/src/butil/third_party/etcdc/sds/sds.c
    ${TOPDIR}/src/butil/third_party/etcdc/cetcd_array.c
    ${TOPDIR}/src/butil/third_party/etcdc/cetcd.c
    ${TOPDIR}/src/butil/arena.cpp
    ${TOPDIR}/src/butil/at_exit.cc
    ${TOPDIR}/src/butil/atomicops_internals_x86_gcc.cc
    ${TOPDIR}/src/butil/base64.cc
    ${TOPDIR}/src/butil/base_switches.cc
    ${TOPDIR}/src/butil/big_endian.cc
    ${TOPDIR}/src/butil/cpu.cc
    ${TOPDIR}/src/butil/debug/alias.cc
    ${TOPDIR}/src/butil/debug/asan_invalid_access.cc
    ${TOPDIR}/src/butil/debug/crash_logging.cc
    ${TOPDIR}/src/butil/debug/debugger.cc
    ${TOPDIR}/src/butil/debug/debugger_posix.cc
    ${TOPDIR}/src/butil/debug/dump_without_crashing.cc
    ${TOPDIR}/src/butil/debug/proc_maps_linux.cc
    ${TOPDIR}/src/butil/debug/stack_trace.cc
    ${TOPDIR}/src/butil/debug/stack_trace_posix.cc
    ${TOPDIR}/src/butil/environment.cc
    ${TOPDIR}/src/butil/files/file.cc
    ${TOPDIR}/src/butil/files/file_posix.cc
    ${TOPDIR}/src/butil/files/file_enumerator.cc
    ${TOPDIR}/src/butil/files/file_enumerator_posix.cc
    ${TOPDIR}/src/butil/files/file_path.cc
    ${TOPDIR}/src/butil/files/file_path_constants.cc
    ${TOPDIR}/src/butil/files/memory_mapped_file.cc
    ${TOPDIR}/src/butil/files/memory_mapped_file_posix.cc
    ${TOPDIR}/src/butil/files/scoped_file.cc
    ${TOPDIR}/src/butil/files/scoped_temp_dir.cc
    ${TOPDIR}/src/butil/file_util.cc
    ${TOPDIR}/src/butil/file_util_linux.cc
    ${TOPDIR}/src/butil/file_util_posix.cc
    ${TOPDIR}/src/butil/guid.cc
    ${TOPDIR}/src/butil/guid_posix.cc
    ${TOPDIR}/src/butil/hash.cc
    ${TOPDIR}/src/butil/lazy_instance.cc
    ${TOPDIR}/src/butil/location.cc
    ${TOPDIR}/src/butil/md5.cc
    ${TOPDIR}/src/butil/memory/aligned_memory.cc
    ${TOPDIR}/src/butil/memory/ref_counted.cc
    ${TOPDIR}/src/butil/memory/ref_counted_memory.cc
    ${TOPDIR}/src/butil/memory/singleton.cc
    ${TOPDIR}/src/butil/memory/weak_ptr.cc
    ${TOPDIR}/src/butil/posix/file_descriptor_shuffle.cc
    ${TOPDIR}/src/butil/posix/global_descriptors.cc
    ${TOPDIR}/src/butil/rand_util.cc
    ${TOPDIR}/src/butil/rand_util_posix.cc
    ${TOPDIR}/src/butil/fast_rand.cpp
    ${TOPDIR}/src/butil/safe_strerror_posix.cc
    ${TOPDIR}/src/butil/sha1_portable.cc
    ${TOPDIR}/src/butil/strings/latin1_string_conversions.cc
    ${TOPDIR}/src/butil/strings/nullable_string16.cc
    ${TOPDIR}/src/butil/strings/safe_sprintf.cc
    ${TOPDIR}/src/butil/strings/string16.cc
    ${TOPDIR}/src/butil/strings/string_number_conversions.cc
    ${TOPDIR}/src/butil/strings/string_split.cc
    ${TOPDIR}/src/butil/strings/string_piece.cc
    ${TOPDIR}/src/butil/strings/string_util.cc
    ${TOPDIR}/src/butil/strings/string_util_constants.cc
    ${TOPDIR}/src/butil/strings/stringprintf.cc
    ${TOPDIR}/src/butil/strings/sys_string_conversions_posix.cc
    ${TOPDIR}/src/butil/strings/utf_offset_string_conversions.cc
    ${TOPDIR}/src/butil/strings/utf_string_conversion_utils.cc
    ${TOPDIR}/src/butil/strings/utf_string_conversions.cc
    ${TOPDIR}/src/butil/synchronization/cancellation_flag.cc
    ${TOPDIR}/src/butil/synchronization/condition_variable_posix.cc
    ${TOPDIR}/src/butil/synchronization/waitable_event_posix.cc
    ${TOPDIR}/src/butil/threading/non_thread_safe_impl.cc
    ${TOPDIR}/src/butil/threading/platform_thread_linux.cc
    ${TOPDIR}/src/butil/threading/platform_thread_posix.cc
    ${TOPDIR}/src/butil/threading/simple_thread.cc
    ${TOPDIR}/src/butil/threading/thread_checker_impl.cc
    ${TOPDIR}/src/butil/threading/thread_collision_warner.cc
    ${TOPDIR}/src/butil/threading/thread_id_name_manager.cc
    ${TOPDIR}/src/butil/threading/thread_local_posix.cc
    ${TOPDIR}/src/butil/threading/thread_local_storage.cc
    ${TOPDIR}/src/butil/threading/thread_local_storage_posix.cc
    ${TOPDIR}/src/butil/threading/thread_restrictions.cc
    ${TOPDIR}/src/butil/threading/watchdog.cc
    ${TOPDIR}/src/butil/time/clock.cc
    ${TOPDIR}/src/butil/time/default_clock.cc
    ${TOPDIR}/src/butil/time/default_tick_clock.cc
    ${TOPDIR}/src/butil/time/tick_clock.cc
    ${TOPDIR}/src/butil/time/time.cc
    ${TOPDIR}/src/butil/time/time_posix.cc
    ${TOPDIR}/src/butil/version.cc
    ${TOPDIR}/src/butil/logging.cc
    ${TOPDIR}/src/butil/class_name.cpp
    ${TOPDIR}/src/butil/errno.cpp
    ${TOPDIR}/src/butil/find_cstr.cpp
    ${TOPDIR}/src/butil/status.cpp
    ${TOPDIR}/src/butil/string_printf.cpp
    ${TOPDIR}/src/butil/thread_local.cpp
    ${TOPDIR}/src/butil/unix_socket.cpp
    ${TOPDIR}/src/butil/endpoint.cpp
    ${TOPDIR}/src/butil/fd_utility.cpp
    ${TOPDIR}/src/butil/files/temp_file.cpp
    ${TOPDIR}/src/butil/files/file_watcher.cpp
    ${TOPDIR}/src/butil/time.cpp
    ${TOPDIR}/src/butil/zero_copy_stream_as_streambuf.cpp
    ${TOPDIR}/src/butil/crc32c.cc
    ${TOPDIR}/src/butil/containers/case_ignored_flat_map.cpp
    ${TOPDIR}/src/butil/iobuf.cpp
    )

aux_source_directory(${TOPDIR}/src/bvar BVAR_SOURCES1)
aux_source_directory(${TOPDIR}/src/bvar/detail BVAR_SOURCES2)

aux_source_directory(${TOPDIR}/src/bthread BTHREAD_SOURCES)

aux_source_directory(${TOPDIR}/src/json2pb JSON2PB_SOURCES)

aux_source_directory(${TOPDIR}/src/brpc BRPC_SOURCES1)
aux_source_directory(${TOPDIR}/src/brpc/details BRPC_SOURCES2)
aux_source_directory(${TOPDIR}/src/brpc/builtin BRPC_SOURCES3)
aux_source_directory(${TOPDIR}/src/brpc/policy BRPC_SOURCES4)

set(MCPACK2PB_SOURCES
	${TOPDIR}/src/mcpack2pb/field_type.cpp
	${TOPDIR}/src/mcpack2pb/mcpack2pb.cpp
	${TOPDIR}/src/mcpack2pb/parser.cpp
	${TOPDIR}/src/mcpack2pb/serializer.cpp
	${TOPDIR}/src/idl_options.pb.cc
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
	)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=0 -DGFLAGS_NS=google -g")	
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS")

set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")


add_definitions(-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)
#add_definitions(-DPROTOBUF_INLINE_NOT_IN_HEADERS=0)

#required by butil/crc32.cc to boost performance for 10x
# gcc version > 40400
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")


############################static library##################################
find_path(GFLAGS_HEADER NAMES gflags/gflags.h)
find_library(GFLAGS_LIB NAMES gflags)

#protobuf 3.2
find_path(PROTOBUF_HEADER NAMES google/protobuf/stubs/common.h PATHS $ENV{PROTOBUF_HOME}/include)
find_library(PROTOBUF_LIB NAMES protobuf PATHS $ENV{PROTOBUF_HOME}/lib)

find_path(LEVELDB_HEADER NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)


############################dynamic library##################################
find_package(Threads)

find_library(RT_LIB NAMES rt)

find_library(SSL_LIB NAMES ssl)

find_library(CRYPTO_LIB NAMES crypto)

find_library(DL_LIB NAMES dl)

find_library(Z_LIB NAMES z)

#protobuf 3.2
find_library(PROTOC_LIB NAMES protoc PATHS $ENV{PROTOBUF_HOME}/lib)

find_path(CURL_HEADER NAMES curl/curl.h)
find_library(CURL_LIB NAMES curl)

find_path(YAJL_HEADER NAMES yajl/yajl_tree.h)
find_library(YAJL_LIB NAMES yajl)

find_path(CPPJSON_HEADER NAMES json/json.h)
find_library(CPPJSON_LIB NAMES cppjson)

##############################################################

include_directories(
        ${GFLAGS_HEADER} 
        ${PROTOBUF_HEADER} 
        ${LEVELDB_HEADER}
        ${CURL_HEADER} 
        ${YAJL_HEADER}
        ${CPPJSON_HEADER}
        )

add_library(brpc SHARED ${SOURCES})


target_link_libraries(brpc 
	${CMAKE_THREAD_LIBS_INIT} 
	${GFLAGS_LIB} 
	${PROTOBUF_LIB} 
	${LEVELDB_LIB} 
	${RT_LIB}
	${SSL_LIB}
	${CRYPTO_LIB}
	${DL_LIB}
	${Z_LIB}
	${PROTOC_LIB}
        ${CURL_LIB}
        ${YAJL_LIB}
        ${CPPJSON_LIB}
	)

	
set(protoc_gen_mcpack_SOURCES
        ${TOPDIR}/src/mcpack2pb/generator.cpp
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

install(
        DIRECTORY src/
        DESTINATION include
        FILES_MATCHING 
	PATTERN "*.h*"
        PATTERN "*.proto"
	)


#######################Test########################
set(EchoClient_SOURCES
        ${TOPDIR}/example/echo/echo.pb.cc
        ${TOPDIR}/example/echo/client.cpp
        )
add_executable(EchoClient ${EchoClient_SOURCES})
target_link_libraries(EchoClient brpc)


set(EchoServer_SOURCES
        ${TOPDIR}/example/echo/echo.pb.cc
        ${TOPDIR}/example/echo/server.cpp
        )
add_executable(EchoServer ${EchoServer_SOURCES})
target_link_libraries(EchoServer brpc)

