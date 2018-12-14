add_executable(traffic_control traffic_control.cpp ${PROTO_SRC})
target_link_libraries(traffic_control brpc-static ${DYNAMIC_LIB})
