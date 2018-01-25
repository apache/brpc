add_executable(trackme_server trackme_server.cpp)
target_link_libraries(trackme_server brpc-static ${DYNAMIC_LIB})
