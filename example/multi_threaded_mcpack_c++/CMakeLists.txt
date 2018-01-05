include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_custom_target(
    CUSTOM_PROTO_MULTI_THREAD ALL
	COMMAND ${PROTOBUF_PROTOC_EXECUTABLE} ${PROTO_FLAGS} --cpp_out=${CMAKE_CURRENT_BINARY_DIR} --proto_path=${PROTOBUF_INCLUDE_DIR} --proto_path=${CMAKE_SOURCE_DIR}/src --proto_path=${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++ --plugin=protoc-gen-mcpack=${CMAKE_CURRENT_BINARY_DIR}/../../src/protoc-gen-mcpack --mcpack_out=${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_SOURCE_DIR}/example/multi_threaded_mcpack_c++/echo.proto
    DEPENDS protoc-gen-mcpack
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBRPC_ENABLE_CPU_PROFILER")

set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc PROPERTIES GENERATED TRUE)

add_executable(multi_threaded_mcpack_client client.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)
add_executable(multi_threaded_mcpack_server server.cpp ${CMAKE_CURRENT_BINARY_DIR}/echo.pb.cc)

add_dependencies(multi_threaded_mcpack_client CUSTOM_PROTO_MULTI_THREAD)
add_dependencies(multi_threaded_mcpack_server CUSTOM_PROTO_MULTI_THREAD)

if(EXAMPLE_LINK_SO)
    target_link_libraries(multi_threaded_mcpack_client brpc ${GPERFTOOLS_LIBRARIES})
    target_link_libraries(multi_threaded_mcpack_server brpc ${GPERFTOOLS_LIBRARIES})
else()
    target_link_libraries(multi_threaded_mcpack_client brpc_static ${DYNAMIC_LIB} ${GPERFTOOLS_LIBRARIES})
    target_link_libraries(multi_threaded_mcpack_server brpc_static ${DYNAMIC_LIB} ${GPERFTOOLS_LIBRARIES})
endif()
