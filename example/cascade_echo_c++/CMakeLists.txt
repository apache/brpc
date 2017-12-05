include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(cascade_echo_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(cascade_echo_client brpc)

add_executable(cascade_echo_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(cascade_echo_server brpc)
