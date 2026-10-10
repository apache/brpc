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

find_path(LEVELDB_INCLUDE_PATH NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)
if ((NOT LEVELDB_INCLUDE_PATH) OR (NOT LEVELDB_LIB))
    message(FATAL_ERROR "Fail to find leveldb")
endif()

if(WITH_SNAPPY)
    find_path(SNAPPY_INCLUDE_PATH NAMES snappy.h)
    find_library(SNAPPY_LIB NAMES snappy)
    if ((NOT SNAPPY_INCLUDE_PATH) OR (NOT SNAPPY_LIB))
        message(FATAL_ERROR "Fail to find snappy")
    endif()
    list(APPEND BRPC_INCLUDE_DIRS ${SNAPPY_INCLUDE_PATH})
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
    # The bundle/package need the selected mode and downloaded header paths.
    set(URMA_USE_MOCK_ENABLED ${URMA_USE_MOCK_ENABLED} PARENT_SCOPE)
    set(URMA_INCLUDE_PATH ${URMA_INCLUDE_PATH} PARENT_SCOPE)
    set(URMA_BOND_INCLUDE_PATH ${URMA_BOND_INCLUDE_PATH} PARENT_SCOPE)
endif()
list(APPEND BRPC_INCLUDE_DIRS ${LEVELDB_INCLUDE_PATH})
add_library(brpc_build_config INTERFACE)
target_link_libraries(brpc_build_config INTERFACE
    mcpack2pb_build_config bthread_build_config)
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
                brpc/urma/urma_handshake.proto)
compile_proto(PROTO_HDRS PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR}
                                    ${CMAKE_CURRENT_SOURCE_DIR}/..
                                    "${PROTO_FILES}")
add_library(PROTO_LIB OBJECT ${PROTO_SRCS} ${PROTO_HDRS})
set_property(TARGET PROTO_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(PROTO_LIB PRIVATE brpc_build_config)

set(brpc_libraries ${LEVELDB_LIB} ZLIB::ZLIB)
set(brpc_private_libs "-lleveldb -lz")
if(WITH_THRIFT)
    list(APPEND brpc_libraries ${THRIFT_LIB})
endif()
if(WITH_SNAPPY)
    list(APPEND brpc_libraries ${SNAPPY_LIB})
    string(APPEND brpc_private_libs " -lsnappy")
endif()
if(WITH_RDMA)
    list(APPEND brpc_libraries ${RDMA_LIB})
endif()
if(WITH_URMA)
    message(STATUS "brpc compile with URMA (mock=${URMA_USE_MOCK_ENABLED})")
    if(NOT URMA_USE_MOCK_ENABLED)
        list(APPEND brpc_libraries ${URMA_LIB})
    endif()
endif()
if(WITH_UBRING)
    message(STATUS "brpc compile with ubring")
    list(APPEND brpc_libraries ${UB_LIB})
endif()

add_library(brpc_runtime_dependencies INTERFACE)
target_link_libraries(brpc_runtime_dependencies INTERFACE ${brpc_libraries})

add_library(BRPC_LIB OBJECT ${BRPC_SOURCES})
target_link_libraries(BRPC_LIB PRIVATE brpc_build_config)
set_property(TARGET BRPC_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET BRPC_LIB PROPERTY BRPC_BUNDLE_LIBRARIES brpc_runtime_dependencies)
set_property(TARGET BRPC_LIB PROPERTY BRPC_PRIVATE_LIBS "${brpc_private_libs}")
add_dependencies(BRPC_LIB PROTO_LIB IDL_OPTIONS_LIB)

if(BRPC_BUILD_COMPONENT_LIBS)
    # Prefer the dependency's package metadata, including static-link dependencies
    # and per-configuration locations. Retain support for system installations
    # that do not provide a CMake package.
    find_package(leveldb CONFIG QUIET)
    if(TARGET leveldb::leveldb)
        add_library(brpc::leveldb INTERFACE IMPORTED)
        set_property(TARGET brpc::leveldb PROPERTY
            INTERFACE_LINK_LIBRARIES leveldb::leveldb)
    else()
        add_library(brpc::leveldb UNKNOWN IMPORTED)
        set_target_properties(brpc::leveldb PROPERTIES
            IMPORTED_LOCATION "${LEVELDB_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${LEVELDB_INCLUDE_PATH}")
    endif()

    set(BRPC_COMPONENT_URMA_REAL OFF)
    if(WITH_URMA AND NOT URMA_USE_MOCK_ENABLED)
        set(BRPC_COMPONENT_URMA_REAL ON)
    endif()
    set(BRPC_COMPONENT_UB_NAME "")
    if(WITH_UBRING AND UB_LIB)
        get_filename_component(BRPC_COMPONENT_UB_NAME "${UB_LIB}" NAME_WE)
        string(REGEX REPLACE "^lib|\\..*$" "" BRPC_COMPONENT_UB_NAME "${BRPC_COMPONENT_UB_NAME}")
    endif()
    foreach(dep THRIFT RDMA SNAPPY URMA UB)
        if((dep STREQUAL "THRIFT" AND WITH_THRIFT) OR
           (dep STREQUAL "RDMA" AND WITH_RDMA) OR
           (dep STREQUAL "SNAPPY" AND WITH_SNAPPY) OR
           (dep STREQUAL "URMA" AND BRPC_COMPONENT_URMA_REAL) OR
           (dep STREQUAL "UB" AND BRPC_COMPONENT_UB_NAME))
            string(TOLOWER ${dep} dep_lower)
            add_library(brpc::${dep_lower} UNKNOWN IMPORTED)
            set_target_properties(brpc::${dep_lower} PROPERTIES
                IMPORTED_LOCATION "${${dep}_LIB}")
            if(${dep}_INCLUDE_PATH)
                set_property(TARGET brpc::${dep_lower} PROPERTY
                    INTERFACE_INCLUDE_DIRECTORIES "${${dep}_INCLUDE_PATH}")
            endif()
        endif()
    endforeach()
    set(BRPC_COMPONENT_URMA_REAL ${BRPC_COMPONENT_URMA_REAL} PARENT_SCOPE)
    set(BRPC_COMPONENT_UB_NAME "${BRPC_COMPONENT_UB_NAME}" PARENT_SCOPE)
    brpc_add_component(brpc BRPC_LIB)
    get_target_property(idl_options_include_dir IDL_OPTIONS_LIB BINARY_DIR)
    target_include_directories(brpc BEFORE PUBLIC
        $<BUILD_INTERFACE:${idl_options_include_dir}>)
    target_compile_definitions(brpc PUBLIC ${BRPC_DEFINITIONS})
    target_sources(brpc PRIVATE $<TARGET_OBJECTS:PROTO_LIB>)
    set_target_properties(brpc PROPERTIES OUTPUT_NAME brpc-only)
    target_link_libraries(brpc PUBLIC brpc::bthread brpc::bvar brpc::butil
        brpc::json2pb brpc::mcpack2pb ZLIB::ZLIB brpc::leveldb)
    foreach(dep THRIFT RDMA SNAPPY URMA UB)
        string(TOLOWER ${dep} dep_lower)
        if(TARGET brpc::${dep_lower})
            target_link_libraries(brpc PUBLIC brpc::${dep_lower})
        endif()
    endforeach()
endif()

brpc_install_module_headers()
brpc_install_headers(${CMAKE_CURRENT_BINARY_DIR} ${PROTO_HDRS})
