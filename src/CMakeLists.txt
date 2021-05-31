if(NOT DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG")
endif()

if(WITH_RDMA)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBRPC_RDMA")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBRPC_RDMA")
endif()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${PROJECT_SOURCE_DIR}/src)

add_library(BUTIL_LIB OBJECT ${BUTIL_SOURCES})
add_library(SOURCES_LIB OBJECT ${SOURCES})

# shared library needs POSITION_INDEPENDENT_CODE
set_property(TARGET ${SOURCES_LIB} PROPERTY POSITION_INDEPENDENT_CODE 1)
set_property(TARGET ${BUTIL_LIB} PROPERTY POSITION_INDEPENDENT_CODE 1)

add_library(brpc-shared SHARED $<TARGET_OBJECTS:BUTIL_LIB> 
                               $<TARGET_OBJECTS:SOURCES_LIB>
                               $<TARGET_OBJECTS:PROTO_LIB>)
add_library(brpc-static STATIC $<TARGET_OBJECTS:BUTIL_LIB>
                               $<TARGET_OBJECTS:SOURCES_LIB>
                               $<TARGET_OBJECTS:PROTO_LIB>)

if(LINK_GFLAGS)
    target_link_libraries(brpc-shared ${DYNAMIC_LIB})
else()
    set(SHARED_LIB ${DYNAMIC_LIB})
    list(REMOVE_ITEM SHARED_LIB ${GFLAGS_LIBRARY})
    target_link_libraries(brpc-shared ${SHARED_LIB})
endif()

if(BRPC_WITH_GLOG)
    target_link_libraries(brpc-shared ${GLOG_LIB})
endif()

if(BRPC_WITH_THRIFT)
    target_link_libraries(brpc-shared thrift)
    target_link_libraries(brpc-static thrift)
endif()

SET_TARGET_PROPERTIES(brpc-static PROPERTIES OUTPUT_NAME brpc CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(brpc-shared PROPERTIES OUTPUT_NAME brpc CLEAN_DIRECT_OUTPUT 1)

# for protoc-gen-mcpack
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/output/bin)
    
set(protoc_gen_mcpack_SOURCES
    ${PROJECT_SOURCE_DIR}/src/mcpack2pb/generator.cpp
 )
add_executable(protoc-gen-mcpack ${protoc_gen_mcpack_SOURCES})

if(LINK_GFLAGS)
    target_link_libraries(protoc-gen-mcpack brpc-shared)
else()
    target_link_libraries(protoc-gen-mcpack brpc-shared ${GFLAGS_LIBRARY})
endif()
    
#install directory
install(TARGETS brpc-shared
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )

install(TARGETS brpc-static
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
