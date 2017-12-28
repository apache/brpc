protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(backup_request_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(backup_request_client brpc)

add_executable(backup_request_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(backup_request_server brpc)
