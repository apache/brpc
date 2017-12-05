include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
execute_process(
	COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_CURRENT_BINARY_DIR}/../../src/ --proto_path=${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/echo.proto
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(echo_ubrpc_compack_client client.cpp echo.pb.cc)
target_link_libraries(echo_ubrpc_compack_client brpc)

add_executable(echo_ubrpc_compack_server server.cpp echo.pb.cc)
target_link_libraries(echo_ubrpc_compack_server brpc)

