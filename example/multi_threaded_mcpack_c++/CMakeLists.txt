include_directories(${CMAKE_CURRENT_BINARY_DIR})
execute_process(
	COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++ ${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++/echo.proto
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(multi_threaded_mcpack_client client.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)
add_executable(multi_threaded_mcpack_server server.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)

if(EXAMPLE_LINK_SO)
    target_link_libraries(multi_threaded_mcpack_client brpc)
    target_link_libraries(multi_threaded_mcpack_server brpc)
else()
    target_link_libraries(multi_threaded_mcpack_client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(multi_threaded_mcpack_server brpc_static ${DYNAMIC_LIB})
endif()
