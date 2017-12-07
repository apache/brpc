cmake_minimum_required(VERSION 3.0)
project(brpc)

option(WITH_GLOG "With glog" OFF)
if(WITH_GLOG)
    set(WITH_GLOG_VAL "1")
else()
    set(WITH_GLOG_VAL "0")
endif()
configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/src/butil/config.h @ONLY)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/example
)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=0 -DGFLAGS_NS=google -g")    
set(CMAKE_CPP_FLAGS "${CMAKE_CPP_FLAGS} -DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS")

set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x -lpthread")
set(CMAKE_C_FLAGS "${CMAKE_CPP_FLAGS} -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer")

add_definitions(-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)
#add_definitions(-DPROTOBUF_INLINE_NOT_IN_HEADERS=0)

#required by butil/crc32.cc to boost performance for 10x
if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 4.4)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4 -msse4.2")
endif()

if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-aligned-new")
endif()

find_path(GFLAGS_HEADER NAMES gflags/gflags.h PATHS $ENV{GFLAGS_HEADER_PATH})
find_library(GFLAGS_LIB NAMES gflags PATHS $ENV{GFLAGS_LIB_PATH})
#find_package(gflags REQUIRED)

#protobuf 3.2
find_path(PROTOBUF_HEADER NAMES google/protobuf/stubs/common.h PATHS $ENV{PB_HEADER_PATH})
find_library(PROTOBUF_LIB NAMES protobuf PATHS $ENV{PB_LIB_PATH})

find_path(LEVELDB_HEADER NAMES leveldb/db.h PATHS $ENV{LEVELDB_HEADER_PATH})
find_library(LEVELDB_LIB NAMES leveldb PATHS $ENV{LEVELDB_LIB_PATH})

if(WITH_GLOG)
    find_path(GLOG_HEADER NAMES glog/logging.h PATHS $ENV{GLOG_HEADER_PATH})
    find_library(GLOG_LIB NAMES glog PATHS $ENV{GLOG_LIB_PATH})
    include_directories(${GLOG_HEADER})
endif()

find_package(Threads)

#protobuf 3.2
find_library(PROTOC_LIB NAMES protoc PATHS $ENV{PB_LIB_PATH})

include_directories(
        ${GFLAGS_HEADER} 
        ${PROTOBUF_HEADER} 
        ${LEVELDB_HEADER}
        )
 
# for *.so
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
# for *.a
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(example)
