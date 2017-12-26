protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(dynamic_partition_echo_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(dynamic_partition_echo_client brpc)

add_executable(dynamic_partition_echo_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(dynamic_partition_echo_server brpc)
