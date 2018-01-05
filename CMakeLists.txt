# Usage:
# export GFLAGS_HEADER_PATH=/usr/local/include
# export GFLAGS_LIB_PATH=/usr/local/lib
# export PB_HEADER_PATH=/usr/local/include
# export PB_LIB_PATH=/usr/local/lib
# export LEVELDB_HEADER_PATH=/usr/local/include
# export LEVELDB_LIB_PATH=/usr/local/lib
# export GLOG_HEADER_PATH=/usr/local/include
# export GLOG_LIB_PATH=/usr/local/lib
# export RT_LIB_PATH=/usr/lib64
# export SSL_LIB_PATH=/usr/lib64
# export CRYPTO_LIB_PATH=/usr/lib64
# export DL_LIB_PATH=/usr/lib64
# export Z_LIB_PATH=/usr/lib64
# export CURL_HEADER_PATH=/usr/include
# export CURL_LIB_PATH=/usr/lib64
# export YAJL_HEADER_PATH=/usr/local/include
# export YAJL_LIB_PATH=/usr/local/lib
# python3 compile_proto.py
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
find_path(GFLAGS_HEADER NAMES gflags/gflags.h PATHS $ENV{GFLAGS_HEADER_PATH})
find_library(GFLAGS_LIB NAMES gflags PATHS $ENV{GFLAGS_LIB_PATH})

#protobuf 3.2
find_path(PROTOBUF_HEADER NAMES google/protobuf/stubs/common.h PATHS $ENV{PB_HEADER_PATH})
find_library(PROTOBUF_LIB NAMES protobuf PATHS $ENV{PB_LIB_PATH})

find_path(LEVELDB_HEADER NAMES leveldb/db.h PATHS $ENV{LEVELDB_HEADER_PATH})
find_library(LEVELDB_LIB NAMES leveldb PATHS $ENV{LEVELDB_LIB_PATH})

if(WITH_GLOG)
    find_path(GLOG_HEADER NAMES glog/logging.h PATHS $ENV{GLOG_HEADER_PATH})
    find_library(GLOG_LIB NAMES glog PATHS $ENV{GLOG_LIB_PATH})
endif()

############################dynamic library##################################
find_package(Threads)

find_library(RT_LIB NAMES rt PATHS $ENV{RT_LIB_PATH})

find_library(SSL_LIB NAMES ssl PATHS $ENV{SSL_LIB_PATH})

find_library(CRYPTO_LIB NAMES crypto PATHS $ENV{CRYPTO_LIB_PATH})

find_library(DL_LIB NAMES dl PATHS $ENV{DL_LIB_PATH})

find_library(Z_LIB NAMES z PATHS $ENV{Z_LIB_PATH})

#protobuf 3.2
find_library(PROTOC_LIB NAMES protoc PATHS $ENV{PB_LIB_PATH})

find_path(CURL_HEADER NAMES curl/curl.h PATHS $ENV{CURL_HEADER_PATH})
find_library(CURL_LIB NAMES curl PATHS $ENV{CURL_LIB_PATH})

find_path(YAJL_HEADER NAMES yajl/yajl_tree.h PATHS $ENV{YAJL_HEADER_PATH})
find_library(YAJL_LIB NAMES yajl PATHS $ENV{YAJL_LIB_PATH})

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
