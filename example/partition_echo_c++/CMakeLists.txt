protobuf_generate_cpp(PROTO_SRC PROTO_HEADER echo.proto)
get_filename_component(HEADER_DIR ${PROTO_HEADER} DIRECTORY)
include_directories(${HEADER_DIR})

add_executable(client client.cpp ${PROTO_SRC} ${PROTO_HEADER})
add_executable(server server.cpp ${PROTO_SRC} ${PROTO_HEADER})

if (EXAMPLE_LINK_SO)
    target_link_libraries(client brpc)
    target_link_libraries(server brpc)
else()
    target_link_libraries(client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(server brpc_static ${DYNAMIC_LIB})
endif()
