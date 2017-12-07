include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(nshead_pb_extension_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(nshead_pb_extension_client brpc)

add_executable(nshead_pb_extension_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(nshead_pb_extension_server brpc)
