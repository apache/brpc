include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(
    CUSTOM_PROTO ALL
	COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/example/echo_c++_ubrpc_compack/ --plugin=protoc-gen-mcpack=${CMAKE_CURRENT_BINARY_DIR}/../../src/protoc-gen-mcpack --mcpack_out=${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/example/echo_c++_ubrpc_compack/echo.proto
    DEPENDS protoc-gen-mcpack
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc PROPERTIES GENERATED TRUE)

add_executable(echo_ubrpc_compack_client client.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)
add_executable(echo_ubrpc_compack_server server.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)

add_dependencies(echo_ubrpc_compack_client CUSTOM_PROTO)
add_dependencies(echo_ubrpc_compack_server CUSTOM_PROTO)

if(EXAMPLE_LINK_SO)
    target_link_libraries(echo_ubrpc_compack_client brpc)
    target_link_libraries(echo_ubrpc_compack_server brpc)
else()
    target_link_libraries(echo_ubrpc_compack_client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(echo_ubrpc_compack_server brpc_static ${DYNAMIC_LIB})
endif()

