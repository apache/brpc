include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(echo_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(echo_client brpc)

add_executable(echo_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(echo_server brpc)
