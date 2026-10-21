# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if(NOT EXISTS "${INCLUDE_DIRS_FILE}")
    message(FATAL_ERROR "Missing include directory list: ${INCLUDE_DIRS_FILE}")
endif()

string(REPLACE "|" ";" PROTOBUF_INCLUDE_DIRS "${PROTOBUF_INCLUDE_DIRS}")
string(REPLACE "|" ";" GENERIC_INCLUDE_DIRS "${GENERIC_INCLUDE_DIRS}")
file(STRINGS "${INCLUDE_DIRS_FILE}" INCLUDE_DIRS)

foreach(protobuf_include_dir IN LISTS PROTOBUF_INCLUDE_DIRS)
    list(FIND INCLUDE_DIRS "${protobuf_include_dir}" protobuf_index)
    if(protobuf_index EQUAL -1)
        message(FATAL_ERROR
            "Selected Protobuf include directory is missing: ${protobuf_include_dir}")
    endif()
    foreach(generic_include_dir IN LISTS GENERIC_INCLUDE_DIRS)
        if(generic_include_dir STREQUAL protobuf_include_dir)
            continue()
        endif()
        list(FIND INCLUDE_DIRS "${generic_include_dir}" generic_index)
        if(NOT generic_index EQUAL -1 AND protobuf_index GREATER generic_index)
            message(FATAL_ERROR
                "Selected Protobuf include directory must precede generic "
                "include directory: ${protobuf_include_dir}")
        endif()
    endforeach()
endforeach()
