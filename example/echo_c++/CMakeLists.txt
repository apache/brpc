cmake_minimum_required(VERSION 2.8.10)
project(echo_c++ C CXX)

# if you want to specify the path of brpc header and lib,
# set the following two variables.
#set(CMAKE_INCLUDE_PATH /home/zjs/gitrepo/brpc/build)
#set(CMAKE_LIBRARY_PATH /home/zjs/gitrepo/brpc/build/lib)

option(EXAMPLE_LINK_SO "Whether examples are linked dynamically" OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindThreads)
include(FindProtobuf)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

find_path(BRPC_HEADER NAMES brpc/server.h)
if(EXAMPLE_LINK_SO)
    find_library(BRPC_LIB NAMES brpc)
else()
    find_library(BRPC_LIB NAMES libbrpc_static.a brpc)
endif()
include_directories(${BRPC_HEADER})
message("BRPC_HEADER=${BRPC_HEADER}")
message("BRPC_LIB=${BRPC_LIB}")

find_path(GFLAGS_INCLUDE_PATH gflags/gflags.h)
find_library(GFLAGS_LIBRARY NAMES gflags libgflags)
include_directories(${GFLAGS_INCLUDE_PATH})

execute_process(
    COMMAND bash -c "grep \"namespace [_A-Za-z0-9]\\+ {\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $2}' | tr -d '\n'"
    OUTPUT_VARIABLE GFLAGS_NS
)
if(${GFLAGS_NS} STREQUAL "GFLAGS_NAMESPACE")
    execute_process(
        COMMAND bash -c "grep \"#define GFLAGS_NAMESPACE [_A-Za-z0-9]\\+\" ${GFLAGS_INCLUDE_PATH}/gflags/gflags_declare.h | head -1 | awk '{print $3}' | tr -d '\n'"
        OUTPUT_VARIABLE GFLAGS_NS
    )
endif()
set(CMAKE_CPP_FLAGS "-DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

find_path(LEVELDB_HEADER NAMES leveldb/db.h)
find_library(LEVELDB_LIB NAMES leveldb)
include_directories(${LEVELDB_HEADER})

add_executable(echo_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
add_executable(echo_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})

set(DYNAMIC_LIB
    ${CMAKE_THREAD_LIBS_INIT}
    ${GFLAGS_LIBRARY}
    ${PROTOBUF_LIBRARIES}
    ${LEVELDB_LIB}
    ssl
    crypto
    dl
    )

if(EXAMPLE_LINK_SO)
    target_link_libraries(echo_client ${BRPC_LIB} ${DYNAMIC_LIB})
    target_link_libraries(echo_server ${BRPC_LIB} ${DYNAMIC_LIB})
else()
    target_link_libraries(echo_client ${BRPC_LIB} ${DYNAMIC_LIB})
    target_link_libraries(echo_server ${BRPC_LIB} ${DYNAMIC_LIB})
endif()
