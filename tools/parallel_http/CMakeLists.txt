add_executable(parallel_http parallel_http.cpp)
target_link_libraries(parallel_http brpc-static ${DYNAMIC_LIB})
