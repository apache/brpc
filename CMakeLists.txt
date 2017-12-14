cmake_minimum_required(VERSION 3.1)
project(brpc C CXX)

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
    #message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
endif()

option(WITH_GLOG "With glog" OFF)
option(BRPC_DEBUG "With debug symbol" ON)
option(BUILD_EXAMPLE "Whether building examples" OFF)
option(BUILD_UNIT_TESTS "Whether building unit tests" OFF)

if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
else()
    set(WITH_GLOG_VAL "0")
endif()

if(BRPC_DEBUG)
    set(DEBUG_SYMBOL "-g")
endif()

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/src/butil/config.h @ONLY)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(GFLAGS REQUIRED)

execute_process(
    COMMAND bash -c "grep \"namespace [_A-Za-z0-9]\\+ {\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}'"
    OUTPUT_VARIABLE GFLAGS_NS
)
# STREQUAL not works. Use MATCHES as workaround
if(${GFLAGS_NS} MATCHES ".*GFLAGS_NAMESPACE.*")
    execute_process(
        COMMAND bash -c "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d \"\\n\"" 
        OUTPUT_VARIABLE GFLAGS_NS
    )
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/example
    ${CMAKE_CURRENT_BINARY_DIR}/src
)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DBUTIL_CXX11_ENABLED")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} ${DEBUG_SYMBOL}")

set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x -std=c++11")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")
SET(CMAKE_EXE_LINKER_FLAGS "-pthread")

#required by butil/crc32.cc to boost performance for 10x
if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.4))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
endif()

if(NOT (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 7.0))
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
endif()

#find_package(Threads REQUIRED)

#find_path(GFLAGS_HEADER NAMES gflags/gflags.h)
#find_library(GFLAGS_LIB NAMES gflags)
#find_package(gflags REQUIRED)

#find_path(THREADS_HEADER NAMES pthread.h)
#find_library(THREADS_LIB NAMES pthread)

find_path(PROTOBUF_HEADER NAMES google/protobuf/stubs/common.h)
find_library(PROTOBUF_LIB NAMES protobuf)

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
        ${PROTOBUF_HEADER}
        ${LEVELDB_HEADER}
        ${THREAD_HEADER}
        #${THREADS_HEADER}
        )

# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

ADD_SUBDIRECTORY(src)
if(BUILD_EXAMPLE)
    ADD_SUBDIRECTORY(example)
endif()
if(BUILD_UNIT_TESTS)
    ADD_SUBDIRECTORY(test)
    enable_testing ()
    add_test (NAME test_butil COMMAND test_butil)
endif()
