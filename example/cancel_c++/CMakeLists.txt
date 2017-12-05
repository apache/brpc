include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(cancel_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(cancel_client brpc)

add_executable(cancel_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(cancel_server brpc)
