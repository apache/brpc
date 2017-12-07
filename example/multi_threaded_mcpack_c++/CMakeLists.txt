include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
execute_process(
	COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++ ${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++/echo.proto
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(multi_threaded_mcpack_client client.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)
target_link_libraries(multi_threaded_mcpack_client brpc)

add_executable(multi_threaded_mcpack_server server.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)
target_link_libraries(multi_threaded_mcpack_server brpc)
