add_executable(redis_cli redis_cli.cpp)
add_executable(redis_press redis_press.cpp)

set(CMAKE_CPP_FLAGS "-DBRPC_WITH_GLOG=${WITH_GLOG_VAL} -DGFLAGS_NS=${GFLAGS_NS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CPP_FLAGS} -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer")
set(AUX_LIB readline ncurses)
if(EXAMPLE_LINK_SO)
    target_link_libraries(redis_cli brpc ${AUX_LIB})
    target_link_libraries(redis_press brpc ${AUX_LIB})
else()
    target_link_libraries(redis_cli brpc_static ${DYNAMIC_LIB} ${AUX_LIB})
    target_link_libraries(redis_press brpc_static ${DYNAMIC_LIB} ${AUX_LIB})
endif()

