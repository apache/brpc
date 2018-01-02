add_executable(memcache_client client.cpp)

if(EXAMPLE_LINK_SO)
    target_link_libraries(memcache_client brpc)
else()
    target_link_libraries(memcache_client brpc_static ${DYNAMIC_LIB})
endif()
