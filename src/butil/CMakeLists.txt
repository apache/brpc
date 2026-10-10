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

set(WITH_GLOG_VAL "0")
if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
    set(BRPC_WITH_GLOG 1)
endif()

set(WITH_CPU_FREQUENCY_VAL "0")
if(WITH_CPU_FREQUENCY)
    set(WITH_CPU_FREQUENCY_VAL "1")
    set(BUTIL_USE_CPU_FREQUENCY 1)
endif()

set(BUTIL_DEFINITIONS
    BRPC_WITH_GLOG=${WITH_GLOG_VAL}
    BUTIL_USE_CPU_FREQUENCY=${WITH_CPU_FREQUENCY_VAL})
if(WITH_MESALINK)
    list(APPEND BUTIL_DEFINITIONS USE_MESALINK)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    include(CheckFunctionExists)
    CHECK_FUNCTION_EXISTS(clock_gettime HAVE_CLOCK_GETTIME)
    if(NOT HAVE_CLOCK_GETTIME)
        list(APPEND BUTIL_DEFINITIONS NO_CLOCK_GETTIME_IN_MAC)
    endif()
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/config.h @ONLY)

set(BUTIL_INCLUDE_DIRS ${GFLAGS_INCLUDE_PATH} ${PROTOBUF_INCLUDE_DIRS})
if(WITH_GLOG)
    find_path(GLOG_INCLUDE_PATH NAMES glog/logging.h)
    find_library(GLOG_LIB NAMES glog)
    if((NOT GLOG_INCLUDE_PATH) OR (NOT GLOG_LIB))
        message(FATAL_ERROR "Fail to find glog")
    endif()
    list(APPEND BUTIL_INCLUDE_DIRS ${GLOG_INCLUDE_PATH})
endif()

if(WITH_MESALINK)
    find_path(MESALINK_INCLUDE_PATH NAMES mesalink/openssl/ssl.h)
    find_library(MESALINK_LIB NAMES mesalink)
    if((NOT MESALINK_INCLUDE_PATH) OR (NOT MESALINK_LIB))
        message(FATAL_ERROR "Fail to find MesaLink")
    else()
        message(STATUS "Found MesaLink: ${MESALINK_LIB}")
    endif()
    list(APPEND BUTIL_INCLUDE_DIRS ${MESALINK_INCLUDE_PATH})
endif()

if(WITH_BORINGSSL)
    find_package(BoringSSL REQUIRED)
    list(APPEND BUTIL_INCLUDE_DIRS ${BORINGSSL_INCLUDE_DIR})
else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND NOT OPENSSL_ROOT_DIR)
        set(OPENSSL_ROOT_DIR
            "/usr/local/opt/openssl" # Homebrew installed OpenSSL
        )
    endif()

    find_package(OpenSSL REQUIRED)
    list(APPEND BUTIL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
endif()

add_library(butil_build_config INTERFACE)
target_link_libraries(butil_build_config INTERFACE brpc_common_config)
target_compile_definitions(butil_build_config INTERFACE ${BUTIL_DEFINITIONS})
target_include_directories(butil_build_config INTERFACE ${BUTIL_INCLUDE_DIRS})

set(butil_crc32c_options)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        # Only CRC32C needs the SSE4.2 intrinsics and feature macro.
        list(APPEND butil_crc32c_options -msse4.2)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        option(WITH_RISCV_ZBC "Enable RISC-V Zbc carry-less multiplication for CRC32C acceleration" OFF)
        option(WITH_RISCV_ZVBC "Enable RISC-V Zvbc vector carry-less multiplication for CRC32C acceleration" OFF)
        if(WITH_RISCV_ZVBC)
            list(APPEND butil_crc32c_options -march=rv64gcv_zbc_zvbc)
            # Preserve the existing RVV base64 implementation as well.
            set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/modp_b64/modp_b64_rvv.cc
                APPEND PROPERTY COMPILE_OPTIONS -march=rv64gcv_zbc_zvbc)
        elseif(WITH_RISCV_ZBC)
            list(APPEND butil_crc32c_options -march=rv64gc_zbc)
        else()
            list(APPEND butil_crc32c_options -march=rv64gc)
        endif()
    endif()
endif()
if(butil_crc32c_options)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/crc32c.cc
        APPEND PROPERTY COMPILE_OPTIONS ${butil_crc32c_options})
endif()

# the reason why not using file(GLOB_RECURSE...) is that we want to
# include different files on different platforms.
set(BUTIL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/arena.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/at_exit.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/atomicops_internals_x86_gcc.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base64.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/base64url.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/big_endian.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/binary_printer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/class_name.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/containers/case_ignored_flat_map.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/cpu.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/crc32c.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/alias.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/asan_invalid_access.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/crash_logging.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/debugger.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/debugger_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/dump_without_crashing.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/proc_maps_linux.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/stack_trace.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/debug/stack_trace_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/endpoint.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/environment.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/errno.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fast_rand.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fd_utility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/file_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/file_util_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_enumerator.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_enumerator_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_path.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_path_constants.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/file_watcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/files/memory_mapped_file.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/memory_mapped_file_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/scoped_file.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/scoped_temp_dir.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/files/temp_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/find_cstr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/guid.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/guid_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/hash.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/iobuf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/iobuf_profiler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lazy_instance.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/location.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/logging.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/aligned_memory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/ref_counted.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/ref_counted_memory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/singleton.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/weak_ptr.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/popen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/posix/file_descriptor_shuffle.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/posix/global_descriptors.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/process_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/rand_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/rand_util_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/recordio.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/safe_strerror_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/sha1_portable.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/single_iobuf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/status.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/string_compare_rvv.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/string_printf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/latin1_string_conversions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/nullable_string16.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/safe_sprintf.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string16.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string_number_conversions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string_piece.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string_split.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string_util.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/string_util_constants.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/stringprintf.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/utf_offset_string_conversions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/utf_string_conversion_utils.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/strings/utf_string_conversions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/synchronization/cancellation_flag.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/synchronization/condition_variable_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/synchronization/waitable_event_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dmg_fp/dtoa_wrapper.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dmg_fp/g_fmt.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dynamic_annotations/dynamic_annotations.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/icu/icu_utf.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/modp_b64/modp_b64.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/modp_b64/modp_b64_rvv.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/murmurhash3/murmurhash3.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snappy/snappy-sinksource.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snappy/snappy-stubs-internal.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/snappy/snappy.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/superfasthash/superfasthash.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/symbolize/demangle.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/symbolize/symbolize.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_key.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thread_local.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/non_thread_safe_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/platform_thread_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/simple_thread.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_checker_impl.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_collision_warner.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_id_name_manager.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_local_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_local_storage.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_local_storage_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/thread_restrictions.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/threading/watchdog.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/time/clock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time/default_clock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time/default_tick_clock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time/tick_clock.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time/time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/time/time_posix.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/unix_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/version.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/zero_copy_stream_as_streambuf.cpp
    )

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(BUTIL_SOURCES ${BUTIL_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/file_util_linux.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/strings/sys_string_conversions_posix.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/threading/platform_thread_linux.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
set(BUTIL_SOURCES ${BUTIL_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/file_util_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/mac/bundle_locations.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/mac/foundation_util.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/mac/scoped_mach_port.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/strings/sys_string_conversions_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/threading/platform_thread_mac.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/time/time_mac.cc)
endif()

set(butil_platform_libraries)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(butil_platform_libraries
        pthread
        "-framework CoreFoundation"
        "-framework CoreGraphics"
        "-framework CoreData"
        "-framework CoreText"
        "-framework Security"
        "-framework Foundation"
        "-Wl,-U,_MallocExtension_ReleaseFreeMemory"
        "-Wl,-U,_ProfilerStart"
        "-Wl,-U,_ProfilerStop"
        "-Wl,-U,__Z13GetStackTracePPvii"
        "-Wl,-U,_mallctl"
        "-Wl,-U,_malloc_stats_print"
    )
endif()

set(butil_libraries ${GFLAGS_LIBRARY} protobuf::libprotobuf
    ${protobuf_ABSL_USED_TARGETS} ${CMAKE_THREAD_LIBS_INIT} dl
    ${butil_platform_libraries})
set(butil_private_libs "-lgflags -lprotobuf -lssl -lcrypto -ldl")
if(WITH_BORINGSSL)
    list(APPEND butil_libraries ${BORINGSSL_SSL_LIBRARY} ${BORINGSSL_CRYPTO_LIBRARY})
elseif(WITH_MESALINK)
    list(APPEND butil_libraries ${OPENSSL_CRYPTO_LIBRARY} ${MESALINK_LIB})
else()
    list(APPEND butil_libraries ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
endif()
if(WITH_GLOG)
    list(APPEND butil_libraries ${GLOG_LIB})
    string(APPEND butil_private_libs " -lglog")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND butil_libraries rt)
    string(APPEND butil_private_libs " -lrt")
endif()

add_library(BUTIL_LIB OBJECT ${BUTIL_SOURCES})
target_link_libraries(BUTIL_LIB PRIVATE butil_build_config)
set_property(TARGET BUTIL_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET BUTIL_LIB PROPERTY BRPC_CRC32C_COMPILE_OPTIONS "${butil_crc32c_options}")
set_property(TARGET BUTIL_LIB PROPERTY BRPC_PLATFORM_LIBRARIES "${butil_platform_libraries}")
set_property(TARGET BUTIL_LIB PROPERTY BRPC_BUNDLE_LIBRARIES "${butil_libraries}")
set_property(TARGET BUTIL_LIB PROPERTY BRPC_PRIVATE_LIBS "${butil_private_libs}")

brpc_install_module_headers()
