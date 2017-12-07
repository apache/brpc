cmake_minimum_required(VERSION 3.0)
project(brpc C CXX)

if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
    message(FATAL_ERROR "GCC is too old, please install a newer version supporting C++11")
endif()

option(WITH_GLOG "With glog" OFF)
if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
else()
    set(WITH_GLOG_VAL "0")
endif()

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/src/butil/config.h @ONLY)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

find_package(Gflags REQUIRED)

execute_process(
    COMMAND bash -c "grep \"namespace [_A-Za-z0-9]\\+ {\" ${Gflags_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}'"
    OUTPUT_VARIABLE GFLAGS_NS
)
# STREQUAL not works. Use MATCHES as workaround
if(${GFLAGS_NS} MATCHES ".*GFLAGS_NAMESPACE.*")
    execute_process(
        COMMAND bash -c "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${Gflags_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d \"\\n\"" 
        OUTPUT_VARIABLE GFLAGS_NS
    )
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/example
    ${CMAKE_CURRENT_BINARY_DIR}/src
)

set(DEBUG_SYMBOLS "-g")

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS")
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} ${DEBUG_SYMBOLS}")

set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")

#add_definitions(-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)
#add_definitions(-DPROTOBUF_INLINE_NOT_IN_HEADERS=0)

#required by butil/crc32.cc to boost performance for 10x
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.4)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
endif()

find_package(Threads REQUIRED)

find_path(GFLAGS_HEADER NAMES gflags/gflags.h PATHS $ENV{GFLAGS_HEADER_PATH})
find_library(GFLAGS_LIB NAMES gflags PATHS $ENV{GFLAGS_LIB_PATH})
#find_package(gflags REQUIRED)

#protobuf 3.2
find_path(PROTOBUF_HEADER NAMES google/protobuf/stubs/common.h PATHS $ENV{PB_HEADER_PATH})
find_library(PROTOBUF_LIB NAMES protobuf PATHS $ENV{PB_LIB_PATH})

find_path(LEVELDB_HEADER NAMES leveldb/db.h PATHS $ENV{LEVELDB_HEADER_PATH})
find_library(LEVELDB_LIB NAMES leveldb PATHS $ENV{LEVELDB_LIB_PATH})

if(WITH_GLOG)
    find_path(GLOG_HEADER NAMES glog/logging.h)
    find_library(GLOG_LIB NAMES glog)
    include_directories(${GLOG_HEADER})
endif()


#protobuf 3.2
find_library(PROTOC_LIB NAMES protoc PATHS $ENV{PB_LIB_PATH})

include_directories(
        ${GFLAGS_HEADER}
        ${PROTOBUF_HEADER}
        ${LEVELDB_HEADER}
        ${THREAD_HEADER}
        )

# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(example)
