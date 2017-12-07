include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(multi_threaded_echo_fns_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(multi_threaded_echo_fns_client brpc)

add_executable(multi_threaded_echo_fns_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(multi_threaded_echo_fns_server brpc)
