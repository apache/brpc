# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

add_library(brpc_bundle_dependencies INTERFACE)

add_subdirectory(butil)
add_subdirectory(bvar)
add_subdirectory(bthread)
add_subdirectory(json2pb)
add_subdirectory(mcpack2pb)
add_subdirectory(brpc)

# Modules report their bundle dependencies after applying their own options.
set(DYNAMIC_LIB)
set(component_private_libs)
foreach(component BUTIL BVAR BTHREAD JSON2PB MCPACK2PB BRPC)
    get_target_property(component_libraries ${component}_LIB BRPC_BUNDLE_LIBRARIES)
    if(component_libraries)
        list(APPEND DYNAMIC_LIB ${component_libraries})
    endif()
    get_target_property(component_pkgconfig ${component}_LIB BRPC_PRIVATE_LIBS)
    if(component_pkgconfig)
        list(APPEND component_private_libs "${component_pkgconfig}")
    endif()
endforeach()
string(JOIN " " BRPC_PRIVATE_LIBS ${component_private_libs})

target_link_libraries(brpc_bundle_dependencies INTERFACE ${DYNAMIC_LIB})
# Keep legacy debug tests and pkg-config generation supplied by the bundle.
set(DYNAMIC_LIB ${DYNAMIC_LIB} PARENT_SCOPE)
set(BRPC_PRIVATE_LIBS "${BRPC_PRIVATE_LIBS}" PARENT_SCOPE)

set(BRPC_SOURCE_OBJECTS)
foreach(component BVAR BTHREAD JSON2PB MCPACK2PB BRPC)
    list(APPEND BRPC_SOURCE_OBJECTS $<TARGET_OBJECTS:${component}_LIB>)
endforeach()

add_library(brpc-static STATIC $<TARGET_OBJECTS:BUTIL_LIB>
                               ${BRPC_SOURCE_OBJECTS}
                               $<TARGET_OBJECTS:PROTO_LIB>)
target_link_libraries(brpc-static PUBLIC brpc_build_config)

SET_TARGET_PROPERTIES(brpc-static PROPERTIES OUTPUT_NAME brpc CLEAN_DIRECT_OUTPUT 1)

if(BUILD_SHARED_LIBS)
    add_library(brpc-shared SHARED $<TARGET_OBJECTS:BUTIL_LIB> 
                                   ${BRPC_SOURCE_OBJECTS}
                                   $<TARGET_OBJECTS:PROTO_LIB>)
    target_link_libraries(brpc-shared PUBLIC brpc_build_config)
    target_link_libraries(brpc-shared PRIVATE ${DYNAMIC_LIB})
    SET_TARGET_PROPERTIES(brpc-shared PROPERTIES OUTPUT_NAME brpc CLEAN_DIRECT_OUTPUT 1)

    install(TARGETS brpc-shared
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            )
endif()



install(TARGETS brpc-static
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
