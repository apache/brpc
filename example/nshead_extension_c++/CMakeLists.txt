add_executable(nshead_extension_client client.cpp)
add_executable(nshead_extension_server server.cpp)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

if(EXAMPLE_LINK_SO)
    target_link_libraries(nshead_extension_client brpc)
    target_link_libraries(nshead_extension_server brpc)
else()
    target_link_libraries(nshead_extension_client brpc_static ${DYNAMIC_LIB})
    target_link_libraries(nshead_extension_server brpc_static ${DYNAMIC_LIB})
endif()
