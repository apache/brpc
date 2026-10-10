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

set(BUTIL_SOURCES ${BUTIL_SOURCES} PARENT_SCOPE)
