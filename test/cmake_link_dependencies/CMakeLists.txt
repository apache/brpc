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

add_executable(cmake_bundle_link_check consumer.cpp)
# Do not add DYNAMIC_LIB here: that would hide missing bundle dependencies.
target_link_libraries(cmake_bundle_link_check PRIVATE brpc-static)
add_executable(cmake_logging_link_check logging.cpp)
target_link_libraries(cmake_logging_link_check PRIVATE brpc-static)

add_custom_target(cmake_link_checks
    COMMAND ${CMAKE_COMMAND}
        -DPKGCONFIG_FILE=${PROJECT_BINARY_DIR}/brpc.pc
        -DWITH_GLOG=${WITH_GLOG} -DWITH_THRIFT=${WITH_THRIFT}
        -DWITH_SNAPPY=${WITH_SNAPPY} -DWITH_RDMA=${WITH_RDMA}
        -DWITH_MESALINK=${WITH_MESALINK}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_pkgconfig.cmake
    COMMAND $<TARGET_FILE:cmake_bundle_link_check>
    COMMAND $<TARGET_FILE:cmake_logging_link_check>
    DEPENDS cmake_bundle_link_check cmake_logging_link_check
    VERBATIM)
