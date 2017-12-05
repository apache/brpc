include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)

add_executable(backup_request_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(backup_request_client brpc)

add_executable(backup_request_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(backup_request_server brpc)
