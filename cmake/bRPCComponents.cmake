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

# Common packaging rules and dependency targets; modules own their libraries.
function(brpc_add_component component object_target)
    add_library(${component} $<TARGET_OBJECTS:${object_target}>)
    add_library(brpc::${component} ALIAS ${component})
    if(ARGN)
        target_sources(${component} PRIVATE ${ARGN})
        set_target_properties(${component} PROPERTIES PUBLIC_HEADER "${ARGN}")
    endif()
    # Generated headers must precede leftovers from in-source Make builds.
    get_filename_component(component_include_dir
        "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    target_include_directories(${component} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<BUILD_INTERFACE:${component_include_dir}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(${component} PUBLIC cxx_std_${BRPC_CXX_STANDARD})
    target_compile_definitions(${component} PUBLIC ${BRPC_COMMON_DEFINITIONS})
    target_link_options(${component} PUBLIC ${BRPC_COMMON_LINK_OPTIONS})
    if(APPLE AND BUILD_SHARED_LIBS)
        # Sibling component dylibs remain discoverable after relocation. Use
        # the same rpath at build/install time to avoid install_name_tool edits.
        set_property(TARGET ${component} APPEND PROPERTY INSTALL_RPATH "@loader_path")
        set_property(TARGET ${component} PROPERTY BUILD_WITH_INSTALL_RPATH TRUE)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND BUILD_SHARED_LIBS)
        set_property(TARGET ${component} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    install(TARGETS ${component} EXPORT brpc-${component}-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT brpc-${component}-targets NAMESPACE brpc::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/brpc)
    export(EXPORT brpc-${component}-targets NAMESPACE brpc::
        FILE ${PROJECT_BINARY_DIR}/brpc-${component}-targets.cmake)
endfunction()

function(brpc_configure_component_package cmake_dir)
    include(CMakePackageConfigHelpers)
    configure_package_config_file(${cmake_dir}/brpc-config.cmake.in
        ${PROJECT_BINARY_DIR}/brpc-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/brpc)
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/brpc-config-version.cmake
        VERSION ${BRPC_VERSION} COMPATIBILITY SameMajorVersion)
    install(FILES ${PROJECT_BINARY_DIR}/brpc-config.cmake
        ${PROJECT_BINARY_DIR}/brpc-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/brpc)
endfunction()
