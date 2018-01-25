include(FindProtobuf)
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER view.proto)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_executable(rpc_view rpc_view.cpp ${PROTO_SRC})
target_link_libraries(rpc_view brpc-static ${DYNAMIC_LIB})
