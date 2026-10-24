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

set(WITH_RDMA_VAL "0")
if(WITH_RDMA)
    set(WITH_RDMA_VAL "1")
endif()

set(WITH_URMA_VAL "0")
if(WITH_URMA)
    set(WITH_URMA_VAL "1")
endif()

string(TOUPPER "${URMA_USE_MOCK}" URMA_USE_MOCK_MODE)
if(NOT URMA_USE_MOCK_MODE STREQUAL "AUTO" AND
   NOT URMA_USE_MOCK_MODE STREQUAL "ON" AND
   NOT URMA_USE_MOCK_MODE STREQUAL "OFF")
    message(FATAL_ERROR
        "URMA_USE_MOCK must be AUTO, ON, or OFF, got: ${URMA_USE_MOCK}")
endif()

set(WITH_UBRING_VAL "0")
if(WITH_UBRING)
        set(WITH_UBRING_VAL "1")
endif()

set(BRPC_DEFINITIONS
    BRPC_WITH_RDMA=${WITH_RDMA_VAL}
    BRPC_WITH_URMA=${WITH_URMA_VAL}
    BRPC_WITH_UBRING=${WITH_UBRING_VAL})
set(BRPC_INCLUDE_DIRS)
if(WITH_FLATBUFFERS)
    find_path(FLATBUFFERS_INCLUDE_DIR NAMES flatbuffers/flatbuffers.h)
    if(NOT FLATBUFFERS_INCLUDE_DIR)
        message(FATAL_ERROR
            "WITH_FLATBUFFERS requires FlatBuffers headers; set FLATBUFFERS_INCLUDE_DIR.")
    endif()
    list(APPEND BRPC_INCLUDE_DIRS ${FLATBUFFERS_INCLUDE_DIR})
endif()
if(WITH_THRIFT)
    list(APPEND BRPC_DEFINITIONS ENABLE_THRIFT_FRAMED_PROTOCOL)
    find_library(THRIFT_LIB NAMES thrift)
    if (NOT THRIFT_LIB)
        message(FATAL_ERROR "Fail to find Thrift")
    endif()
endif()

if(WITH_THRIFT)
    find_path(THRIFT_INCLUDE_PATH NAMES thrift/Thrift.h)
    if(NOT THRIFT_INCLUDE_PATH)
        message(FATAL_ERROR "Fail to find Thrift headers")
    endif()
    list(APPEND BRPC_INCLUDE_DIRS ${THRIFT_INCLUDE_PATH})
endif()
find_package(ZLIB REQUIRED)
list(APPEND BRPC_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS})

# Older LevelDB packages reference Threads::Threads without finding it.
find_package(Threads REQUIRED)
find_package(leveldb CONFIG QUIET)
add_library(brpc_leveldb INTERFACE)
set(brpc_pkgconfig_needs_snappy ${WITH_SNAPPY})
if(TARGET leveldb::leveldb)
    # LevelDB 1.23 exports its private -Werror/-Wthread-safety diagnostics.
    # Filter only these known packaging mistakes from the imported target's
    # in-memory interface, without editing the installed package.
    get_target_property(leveldb_compile_options leveldb::leveldb INTERFACE_COMPILE_OPTIONS)
    if(leveldb_compile_options)
        list(REMOVE_ITEM leveldb_compile_options -Werror -Wthread-safety)
        set_property(TARGET leveldb::leveldb PROPERTY
            INTERFACE_COMPILE_OPTIONS "${leveldb_compile_options}")
    endif()
    target_link_libraries(brpc_leveldb INTERFACE leveldb::leveldb)

    # The legacy .pc file needs its own representation of the dependency.
    get_target_property(leveldb_dependencies leveldb::leveldb INTERFACE_LINK_LIBRARIES)
    get_target_property(leveldb_type leveldb::leveldb TYPE)
    if(leveldb_type STREQUAL "SHARED_LIBRARY" AND leveldb_dependencies)
        # LevelDB 1.23 also exports bare "snappy" for shared libraries.
        # The loader resolves that dependency; consumers do not need
        # Snappy's development library. Keep it for static/unknown library types.
        list(REMOVE_ITEM leveldb_dependencies snappy "$<LINK_ONLY:snappy>")
        set_property(TARGET leveldb::leveldb PROPERTY
            INTERFACE_LINK_LIBRARIES "${leveldb_dependencies}")
    endif()
    foreach(dependency IN LISTS leveldb_dependencies)
        if(dependency MATCHES "(^|[:;])([Ss]nappy::)?snappy($|[;>])")
            set(brpc_pkgconfig_needs_snappy ON)
        endif()
        if((dependency STREQUAL "snappy" OR
            dependency STREQUAL "$<LINK_ONLY:snappy>") AND NOT TARGET snappy)
            # Old exports use bare "snappy" without finding it or supplying a
            # search directory. Resolve that link item for non-system prefixes.
            find_library(SNAPPY_LIB NAMES snappy)
            if(NOT SNAPPY_LIB)
                message(FATAL_ERROR "LevelDB requires Snappy, but it was not found")
            endif()
            add_library(snappy UNKNOWN IMPORTED)
            set_property(TARGET snappy PROPERTY IMPORTED_LOCATION "${SNAPPY_LIB}")
        endif()
    endforeach()
else()
    # Keep supporting installations without a CMake package configuration.
    find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
    find_library(LEVELDB_LIB NAMES leveldb)
    if((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
        message(FATAL_ERROR "Fail to find leveldb")
    endif()
    target_include_directories(brpc_leveldb SYSTEM INTERFACE ${LEVELDB_INCLUDE_PATH})
    target_link_libraries(brpc_leveldb INTERFACE ${LEVELDB_LIB})
endif()
if(WITH_SNAPPY)
    find_library(SNAPPY_LIB NAMES snappy)
    if(NOT SNAPPY_LIB)
        message(FATAL_ERROR "Fail to find snappy")
    endif()
endif()

if(WITH_RDMA)
    message("brpc compile with rdma")
    find_path(RDMA_INCLUDE_PATH NAMES infiniband/verbs.h)
    find_library(RDMA_LIB NAMES ibverbs)
    if((NOT RDMA_INCLUDE_PATH) OR (NOT RDMA_LIB))
        message(FATAL_ERROR "Fail to find ibverbs")
    endif()
    list(APPEND BRPC_INCLUDE_DIRS ${RDMA_INCLUDE_PATH})
endif()

if(WITH_URMA)
    # UrmaTransport and its link-time mock both compile against the upstream
    # UMDK API. Prefer installed headers; otherwise fetch a pinned upstream
    # commit, following Mooncake's URMA mock setup.
    find_path(URMA_INCLUDE_PATH NAMES urma_api.h
              HINTS ENV URMA_ROOT
              PATHS /usr/include /usr/local/include
              PATH_SUFFIXES ub/umdk/urma umdk/urma urma
                            src/urma/lib/urma/core/include)
    find_path(URMA_BOND_INCLUDE_PATH NAMES urma_ubagg.h
              HINTS ENV URMA_ROOT
              PATHS /usr/include /usr/local/include
              PATH_SUFFIXES ub/umdk/urma umdk/urma urma
                            src/urma/lib/urma/bond/include)
    if(NOT URMA_INCLUDE_PATH AND DOWNLOAD_URMA_HEADERS)
        include(FetchContent)
        FetchContent_Declare(
            urma_headers
            GIT_REPOSITORY https://atomgit.com/openeuler/umdk.git
            GIT_TAG 564ee727a55523d4351a8fb3c94292b388ebb924
            # brpc only consumes UMDK headers; avoid cloning unrelated UMDK
            # submodules such as CAM dependencies during configuration.
            GIT_SUBMODULES ""
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(urma_headers)
        if(NOT urma_headers_POPULATED)
            FetchContent_Populate(urma_headers)
        endif()
        set(URMA_INCLUDE_PATH
            "${urma_headers_SOURCE_DIR}/src/urma/lib/urma/core/include")
        set(URMA_BOND_INCLUDE_PATH
            "${urma_headers_SOURCE_DIR}/src/urma/lib/urma/bond/include")
        message(STATUS "Using downloaded UMDK headers: ${URMA_INCLUDE_PATH}")
    endif()
    if(NOT URMA_INCLUDE_PATH)
        message(FATAL_ERROR
            "Fail to find urma_api.h. Install UMDK headers, set URMA_ROOT, "
            "or enable DOWNLOAD_URMA_HEADERS.")
    endif()

    find_library(URMA_LIB NAMES urma
                 HINTS ENV URMA_ROOT
                 PATH_SUFFIXES lib lib64)
    if(URMA_USE_MOCK_MODE STREQUAL "ON")
        message(STATUS "URMA_USE_MOCK=ON; forcing the link-time URMA mock")
        set(URMA_USE_MOCK_ENABLED 1)
    elseif(URMA_USE_MOCK_MODE STREQUAL "OFF")
        if(NOT URMA_LIB)
            message(FATAL_ERROR
                "URMA_USE_MOCK=OFF requires liburma; install it or use AUTO/ON.")
        endif()
        message(STATUS "URMA_USE_MOCK=OFF; using liburma")
        set(URMA_USE_MOCK_ENABLED 0)
    elseif(URMA_LIB)
        message(STATUS "Found URMA library: ${URMA_LIB}")
        set(URMA_USE_MOCK_ENABLED 0)
    else()
        message(STATUS
            "liburma not found; building with the URMA link-time mock")
        set(URMA_USE_MOCK_ENABLED 1)
    endif()
endif()

if(WITH_URMA)
    list(APPEND BRPC_INCLUDE_DIRS ${URMA_INCLUDE_PATH})
    if(URMA_BOND_INCLUDE_PATH)
        list(APPEND BRPC_INCLUDE_DIRS ${URMA_BOND_INCLUDE_PATH})
    endif()
endif()
add_library(brpc_build_config INTERFACE)
target_link_libraries(brpc_build_config INTERFACE bthread_build_config)
if(WITH_URMA AND URMA_USE_MOCK_ENABLED)
    # The link-time mock uses std::shared_mutex, which requires C++17.
    target_compile_features(brpc_build_config INTERFACE cxx_std_17)
endif()
target_include_directories(brpc_build_config BEFORE INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(brpc_build_config INTERFACE ${BRPC_DEFINITIONS})
target_include_directories(brpc_build_config INTERFACE ${BRPC_INCLUDE_DIRS})

function(check_thrift_version target_arg)
    #use thrift command to get version
    execute_process(
        COMMAND thrift --version
        OUTPUT_VARIABLE THRIFT_VERSION_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" THRIFT_VERSION ${THRIFT_VERSION_OUTPUT})
    string(REGEX REPLACE "\\." ";" THRIFT_VERSION_LIST ${THRIFT_VERSION})

    list(GET THRIFT_VERSION_LIST 0 THRIFT_MAJOR_VERSION)
    list(GET THRIFT_VERSION_LIST 1 THRIFT_MINOR_VERSION)

    if (THRIFT_MAJOR_VERSION EQUAL 0 AND THRIFT_MINOR_VERSION LESS 11)
        message(STATUS "Thrift version is less than 0.11.0")
        target_compile_definitions(${target_arg} INTERFACE _THRIFT_VERSION_LOWER_THAN_0_11_0_)
    else()
        message(STATUS "Thrift version is equal to or greater than 0.11.0")
    endif()
endfunction()

if(WITH_THRIFT)
    check_thrift_version(brpc_build_config)
endif()

file(GLOB_RECURSE BRPC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB_RECURSE THRIFT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/thrift*.cpp")
if(NOT WITH_FLATBUFFERS)
    list(FILTER BRPC_SOURCES EXCLUDE REGEX "/brpc/flatbuffers/.*\\.cpp$")
    list(REMOVE_ITEM BRPC_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/policy/flatbuffers_protocol.cpp")
endif()
file(GLOB_RECURSE EXCLUDE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/event_dispatcher_*.cpp")

# When building with the real liburma, exclude the link-time mock so its urma_*
# symbols do not clash with the library. When liburma is absent, keep it so CI
# can build and test UrmaTransport without URMA hardware.
if(WITH_URMA AND NOT URMA_USE_MOCK_ENABLED)
    list(REMOVE_ITEM BRPC_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/urma/mock_urma.cpp")
endif()

if(WITH_THRIFT)
    message("brpc compile with thrift protocol")
else()
    # Remove thrift sources
    foreach(v ${THRIFT_SOURCES})
        list(REMOVE_ITEM BRPC_SOURCES ${v})
    endforeach()
    set(THRIFT_SOURCES "")
endif()

foreach(v ${EXCLUDE_SOURCES})
    list(REMOVE_ITEM BRPC_SOURCES ${v})
endforeach()

include(CompileProto)
set(PROTO_FILES brpc/builtin_service.proto
                brpc/errno.proto
                brpc/get_favicon.proto
                brpc/get_js.proto
                brpc/grpc_health_check.proto
                brpc/nshead_meta.proto
                brpc/options.proto
                brpc/policy/baidu_rpc_meta.proto
                brpc/policy/hulu_pbrpc_meta.proto
                brpc/policy/mongo.proto
                brpc/policy/public_pbrpc_meta.proto
                brpc/policy/sofa_pbrpc_meta.proto
                brpc/proto_base.proto
                brpc/rdma/rdma_handshake.proto
                brpc/rpc_dump.proto
                brpc/rtmp.proto
                brpc/span.proto
                brpc/streaming_rpc_meta.proto
                brpc/trackme.proto
                brpc/urma/urma_handshake.proto
                idl_options.proto)
compile_proto(PROTO_HDRS PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}
                                    ${CMAKE_CURRENT_SOURCE_DIR}/..
                                    "${PROTO_FILES}")
add_library(PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET PROTO_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(PROTO_LIB PRIVATE brpc_build_config)

set(brpc_libraries brpc_leveldb)
set(brpc_private_libs "-lleveldb")
if(WITH_THRIFT)
    list(APPEND brpc_libraries ${THRIFT_LIB})
    string(APPEND brpc_private_libs " -lthrift")
endif()
if(WITH_SNAPPY)
    list(APPEND brpc_libraries ${SNAPPY_LIB})
endif()
if(brpc_pkgconfig_needs_snappy)
    string(APPEND brpc_private_libs " -lsnappy")
endif()
if(WITH_RDMA)
    list(APPEND brpc_libraries ${RDMA_LIB})
    string(APPEND brpc_private_libs " -libverbs")
endif()
if(WITH_URMA)
    message(STATUS "brpc compile with URMA (mock=${URMA_USE_MOCK_ENABLED})")
    if(NOT URMA_USE_MOCK_ENABLED)
        list(APPEND brpc_libraries ${URMA_LIB})
        string(APPEND brpc_private_libs " -lurma")
    endif()
endif()
if(WITH_UBRING)
    message(STATUS "brpc compile with ubring")
    list(APPEND brpc_libraries ${UB_LIB})
    if(UB_LIB)
        get_filename_component(ub_name "${UB_LIB}" NAME_WE)
        string(REGEX REPLACE "^lib|\\..*$" "" ub_name "${ub_name}")
        string(APPEND brpc_private_libs " -l${ub_name}")
    endif()
endif()

add_library(brpc_runtime_dependencies INTERFACE)
target_link_libraries(brpc_runtime_dependencies INTERFACE ${brpc_libraries})
add_library(brpc_system_dependencies INTERFACE)
target_link_libraries(brpc_system_dependencies INTERFACE ZLIB::ZLIB)

add_library(BRPC_LIB OBJECT ${BRPC_SOURCES})
target_link_libraries(BRPC_LIB PRIVATE brpc_build_config brpc_leveldb)
set_property(TARGET BRPC_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET BRPC_LIB PROPERTY BRPC_BUNDLE_LIBRARIES brpc_runtime_dependencies)
set_property(TARGET BRPC_LIB PROPERTY BRPC_PRIVATE_LIBS "${brpc_private_libs}")
set_property(TARGET BRPC_LIB PROPERTY BRPC_BUNDLE_SYSTEM_LIBRARIES brpc_system_dependencies)
set_property(TARGET BRPC_LIB PROPERTY BRPC_PRIVATE_SYSTEM_LIBS "-lz")
add_dependencies(BRPC_LIB PROTO_LIB)

brpc_install_module_headers()
brpc_install_headers(${CMAKE_CURRENT_BINARY_DIR} ${PROTO_HDRS})
