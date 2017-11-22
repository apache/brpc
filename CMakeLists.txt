# Usage:
# export PROTOBUF_HOME=/usr/local/protobuf3
# python compile_proto.py
# mkdir build 
# cd build 
# cmake ..
# make 
# make install
# Written by Kevin.XU @ 2017.11.16


cmake_minimum_required(VERSION 3.0)
project(brpc)


#message("CMAKE_SOURCE_DIR= ${CMAKE_SOURCE_DIR}")
#message("PROJECT_SOURCE_DIR= ${PROJECT_SOURCE_DIR}")

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

if(WITH_GLOG)
    find_path(GLOG_HEADER NAMES glog/logging.h)
    find_library(GLOG_LIB NAMES glog)
endif()

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

##############################################################

include_directories(
        ${GFLAGS_HEADER} 
        ${PROTOBUF_HEADER} 
        ${LEVELDB_HEADER}
        ${CURL_HEADER} 
        ${YAJL_HEADER}
        )

if(WITH_GLOG)
        include_directories(${GLOG_HEADER})
endif()

ADD_SUBDIRECTORY(src)
ADD_SUBDIRECTORY(example)
