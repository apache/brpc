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

# Install headers from their owners, without a build-tree staging directory.
function(brpc_install_module_headers)
    get_filename_component(include_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    list(FILTER headers EXCLUDE REGEX "\\.pb\\.h$")
    brpc_install_headers(${include_root} ${headers})
endfunction()

function(brpc_install_headers include_root)
    foreach(header IN LISTS ARGN)
        file(RELATIVE_PATH relative_header ${include_root} ${header})
        get_filename_component(include_subdir ${relative_header} DIRECTORY)
        install(FILES ${header}
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${include_subdir})
    endforeach()
endfunction()
