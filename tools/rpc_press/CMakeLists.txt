file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/tools/rpc_press/*.cpp")
add_executable(rpc_press ${SOURCES})
target_link_libraries(rpc_press brpc_static ${DYNAMIC_LIB})
