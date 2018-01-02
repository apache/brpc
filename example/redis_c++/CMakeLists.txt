add_executable(redis_cli redis_cli.cpp)
add_executable(redis_press redis_press.cpp)

if(EXAMPLE_LINK_SO)
    target_link_libraries(redis_cli brpc)
    target_link_libraries(redis_press brpc)
else()
    target_link_libraries(redis_cli brpc_static ${DYNAMIC_LIB})
    target_link_libraries(redis_press brpc_static ${DYNAMIC_LIB})
endif()

