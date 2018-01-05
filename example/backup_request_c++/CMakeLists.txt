protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

add_executable(backup_request_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
add_executable(backup_request_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})

if(EXAMPLE_LINK_SO)
    target_link_libraries(backup_request_client brpc)
    target_link_libraries(backup_request_server brpc)
else()
    target_link_libraries(backup_request_client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(backup_request_server brpc_static ${DYNAMIC_LIB})
endif()
