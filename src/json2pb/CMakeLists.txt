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

file(GLOB_RECURSE JSON2PB_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(JSON2PB_LIB OBJECT ${JSON2PB_SOURCES})
target_link_libraries(JSON2PB_LIB PRIVATE butil_build_config)
set_property(TARGET JSON2PB_LIB PROPERTY POSITION_INDEPENDENT_CODE 1)

if(BRPC_BUILD_COMPONENT_LIBS)
    brpc_add_component(json2pb JSON2PB_LIB)
    target_link_libraries(json2pb PUBLIC brpc::butil protobuf::libprotobuf)
endif()

brpc_install_module_headers()
