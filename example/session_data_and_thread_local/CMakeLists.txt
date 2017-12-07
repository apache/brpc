include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(session_data_and_thread_local_client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(session_data_and_thread_local_client brpc)

add_executable(session_data_and_thread_local_server server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(session_data_and_thread_local_server brpc)
