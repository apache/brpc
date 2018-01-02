protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(asynchronous_echo_client client.cpp ${PROTO_SRC})
add_executable(asynchronous_echo_server server.cpp ${PROTO_SRC})

if(EXAMPLE_LINK_SO)
    target_link_libraries(asynchronous_echo_client brpc)
    target_link_libraries(asynchronous_echo_server brpc)
else()
    target_link_libraries(asynchronous_echo_client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(asynchronous_echo_server brpc_static ${DYNAMIC_LIB})
endif()
