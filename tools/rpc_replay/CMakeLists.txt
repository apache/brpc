file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/tools/rpc_replay/*.cpp")
add_executable(rpc_replay ${SOURCES})
target_link_libraries(rpc_replay brpc-static ${DYNAMIC_LIB})
