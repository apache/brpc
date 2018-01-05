add_executable(memcache_client client.cpp)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")

if(EXAMPLE_LINK_SO)
    target_link_libraries(memcache_client brpc)
else()
    target_link_libraries(memcache_client brpc_static ${DYNAMIC_LIB})
endif()
