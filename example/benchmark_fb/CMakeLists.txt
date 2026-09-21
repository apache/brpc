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

cmake_minimum_required(VERSION 3.16...3.28)
project(benchmark_fb LANGUAGES CXX)

set(BRPC_ROOT "" CACHE PATH "FlatBuffers-enabled brpc output or install prefix")
set(BRPC_EXTRA_LIBRARIES "" CACHE STRING "Extra dependencies of this libbrpc, as a CMake list")
option(LINK_SO "Link the shared brpc library instead of libbrpc.a" OFF)
if(NOT BRPC_ROOT)
    message(FATAL_ERROR "Set BRPC_ROOT to an existing WITH_FLATBUFFERS=ON output/install prefix")
endif()
get_filename_component(BRPC_ROOT "${BRPC_ROOT}" ABSOLUTE)
set(BRPC_INCLUDE_DIR "${BRPC_ROOT}/include")
if(NOT EXISTS "${BRPC_INCLUDE_DIR}/brpc/channel.h" OR
   NOT EXISTS "${BRPC_INCLUDE_DIR}/butil/config.h")
    message(FATAL_ERROR "BRPC_ROOT must contain include/brpc/channel.h and include/butil/config.h")
endif()
file(STRINGS "${BRPC_INCLUDE_DIR}/butil/config.h" _fb_enabled
    REGEX "^#[ \t]*define[ \t]+BRPC_WITH_FLATBUFFERS[ \t]+1([ \t]|$)")
if(NOT _fb_enabled)
    message(FATAL_ERROR
        "BRPC_ROOT is not FlatBuffers-enabled. Rebuild brpc with WITH_FLATBUFFERS=ON; do not force BRPC_WITH_FLATBUFFERS with -D.")
endif()
# Re-resolve when BRPC_ROOT or LINK_SO changes; never select another installed brpc.
unset(_BRPC_LIBRARY CACHE)
if(LINK_SO)
    set(_brpc_names "${CMAKE_SHARED_LIBRARY_PREFIX}brpc${CMAKE_SHARED_LIBRARY_SUFFIX}")
else()
    set(_brpc_names libbrpc.a)
endif()
find_library(_BRPC_LIBRARY NAMES ${_brpc_names}
    PATHS "${BRPC_ROOT}/lib" "${BRPC_ROOT}/lib64" NO_DEFAULT_PATH)
if(NOT _BRPC_LIBRARY)
    message(FATAL_ERROR "The requested brpc library was not found under BRPC_ROOT/lib or lib64")
endif()

find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h)
find_program(FLATC_EXECUTABLE NAMES flatc)
find_program(BRPC_FLATC_EXECUTABLE NAMES brpc_flatc HINTS "${BRPC_ROOT}/bin")
if(NOT FLATBUFFERS_INCLUDE_DIR OR NOT FLATC_EXECUTABLE OR NOT BRPC_FLATC_EXECUTABLE)
    message(FATAL_ERROR "Set FLATBUFFERS_INCLUDE_DIR, FLATC_EXECUTABLE and BRPC_FLATC_EXECUTABLE")
endif()
if(NOT EXISTS "${FLATBUFFERS_INCLUDE_DIR}/flatbuffers/base.h")
    message(FATAL_ERROR "FLATBUFFERS_INCLUDE_DIR must contain flatbuffers/base.h")
endif()
foreach(_part MAJOR MINOR REVISION)
    file(STRINGS "${FLATBUFFERS_INCLUDE_DIR}/flatbuffers/base.h" _version_line
        REGEX "^#define[ \t]+FLATBUFFERS_VERSION_${_part}[ \t]+[0-9]+")
    if(NOT _version_line MATCHES "FLATBUFFERS_VERSION_${_part}[ \t]+([0-9]+)")
        message(FATAL_ERROR "Cannot read FlatBuffers ${_part} version from base.h")
    endif()
    set(_fb_${_part} "${CMAKE_MATCH_1}")
endforeach()
set(_headers_version "${_fb_MAJOR}.${_fb_MINOR}.${_fb_REVISION}")
execute_process(COMMAND "${FLATC_EXECUTABLE}" --version
    RESULT_VARIABLE _flatc_result OUTPUT_VARIABLE _flatc_version
    ERROR_VARIABLE _flatc_error OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 5)
if(NOT _flatc_result STREQUAL "0" OR
   NOT _flatc_version MATCHES "flatc version ([0-9]+\\.[0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Cannot execute official flatc --version: ${_flatc_error}")
endif()
set(_compiler_version "${CMAKE_MATCH_1}")
if(NOT _compiler_version VERSION_EQUAL _headers_version)
    message(FATAL_ERROR
        "FlatBuffers headers ${_headers_version} do not match flatc ${_compiler_version}")
endif()
execute_process(COMMAND "${BRPC_FLATC_EXECUTABLE}" --help
    RESULT_VARIABLE _brpc_flatc_result OUTPUT_QUIET ERROR_VARIABLE _brpc_flatc_error TIMEOUT 5)
if(NOT _brpc_flatc_result STREQUAL "0")
    message(FATAL_ERROR "Cannot execute BRPC_FLATC_EXECUTABLE: ${_brpc_flatc_error}")
endif()

find_package(Threads REQUIRED)
find_package(Protobuf CONFIG QUIET)
set(_protobuf_config ${Protobuf_FOUND})
if(NOT Protobuf_FOUND)
    find_package(Protobuf REQUIRED)
endif()
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_path(GFLAGS_INCLUDE_DIR NAMES gflags/gflags.h)
find_library(GFLAGS_LIBRARY NAMES gflags)
find_library(LEVELDB_LIBRARY NAMES leveldb)
if(NOT GFLAGS_INCLUDE_DIR OR NOT GFLAGS_LIBRARY OR NOT LEVELDB_LIBRARY)
    message(FATAL_ERROR "Install gflags and leveldb, or set CMAKE_PREFIX_PATH to the runtime dependency prefix")
endif()
set(_cxx_standard 14)
if(Protobuf_VERSION VERSION_GREATER 4.21)
    set(_cxx_standard 17)
    if(NOT _protobuf_config)
        message(FATAL_ERROR "Protobuf 5+ needs its CMake config package to export transitive Abseil dependencies")
    endif()
endif()

add_library(benchmark_fb_dependencies INTERFACE)
# Keep the actual runtime configuration ahead of dependency include directories.
target_include_directories(benchmark_fb_dependencies INTERFACE
    "${BRPC_INCLUDE_DIR}" "${FLATBUFFERS_INCLUDE_DIR}" "${GFLAGS_INCLUDE_DIR}")
target_compile_features(benchmark_fb_dependencies INTERFACE cxx_std_${_cxx_standard})
target_link_libraries(benchmark_fb_dependencies INTERFACE
    "${_BRPC_LIBRARY}" protobuf::libprotobuf "${GFLAGS_LIBRARY}" "${LEVELDB_LIBRARY}"
    OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS}
    ${BRPC_EXTRA_LIBRARIES})
file(STRINGS "${BRPC_INCLUDE_DIR}/butil/config.h" _glog_enabled
    REGEX "^#[ \t]*define[ \t]+BRPC_WITH_GLOG[ \t]+1([ \t]|$)")
if(_glog_enabled)
    find_package(glog CONFIG REQUIRED)
    target_link_libraries(benchmark_fb_dependencies INTERFACE glog::glog)
endif()
if(APPLE)
    target_link_libraries(benchmark_fb_dependencies INTERFACE
        "-framework CoreFoundation" "-framework CoreGraphics" "-framework CoreData"
        "-framework CoreText" "-framework Security" "-framework Foundation")
    target_link_options(benchmark_fb_dependencies INTERFACE
        "LINKER:-U,_MallocExtension_ReleaseFreeMemory" "LINKER:-U,_ProfilerStart"
        "LINKER:-U,_ProfilerStop" "LINKER:-U,__Z13GetStackTracePPvii"
        "LINKER:-U,_mallctl" "LINKER:-U,_malloc_stats_print")
endif()

set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(_schema "${CMAKE_CURRENT_SOURCE_DIR}/echo.fbs")
set(_generated_files "${_generated}/echo_generated.h"
    "${_generated}/echo.brpc.fb.h" "${_generated}/echo.brpc.fb.cpp")
add_custom_command(OUTPUT ${_generated_files}
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_generated}"
    COMMAND "${FLATC_EXECUTABLE}" --cpp -o "${_generated}" "${_schema}"
    COMMAND "${BRPC_FLATC_EXECUTABLE}" -o "${_generated}" "${_schema}"
    DEPENDS "${_schema}" "${FLATC_EXECUTABLE}" "${BRPC_FLATC_EXECUTABLE}"
    VERBATIM)
add_custom_target(benchmark_fb_codegen DEPENDS ${_generated_files})
add_library(benchmark_fb_schema STATIC "${_generated}/echo.brpc.fb.cpp")
add_dependencies(benchmark_fb_schema benchmark_fb_codegen)
target_include_directories(benchmark_fb_schema PUBLIC "${_generated}")
target_link_libraries(benchmark_fb_schema PUBLIC benchmark_fb_dependencies)
foreach(_program client server)
    add_executable(benchmark_fb_${_program} "${_program}.cpp")
    add_dependencies(benchmark_fb_${_program} benchmark_fb_codegen)
    target_link_libraries(benchmark_fb_${_program} PRIVATE benchmark_fb_schema)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(benchmark_fb_${_program} PRIVATE -Wall -Wextra)
    endif()
endforeach()

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME benchmark_fb_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/smoke.py"
            --server "$<TARGET_FILE:benchmark_fb_server>"
            --client "$<TARGET_FILE:benchmark_fb_client>")
    set_tests_properties(benchmark_fb_smoke PROPERTIES TIMEOUT 50 LABELS "flatbuffers;smoke")
endif()
message(STATUS "benchmark_fb: brpc=${_BRPC_LIBRARY}, FlatBuffers=${_headers_version}")
