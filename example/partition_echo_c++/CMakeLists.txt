include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(client brpc)

add_executable(server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(server brpc)
