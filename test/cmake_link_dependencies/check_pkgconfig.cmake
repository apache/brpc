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

cmake_minimum_required(VERSION 3.16)
file(READ "${PKGCONFIG_FILE}" metadata)
if(NOT metadata MATCHES "\nLibs.private: ([^\n]*)")
    message(FATAL_ERROR "Missing private dependency metadata")
endif()
separate_arguments(libraries UNIX_COMMAND "${CMAKE_MATCH_1}")
foreach(required -lprotoc -lleveldb -lgflags -lprotobuf -lcrypto -lz -ldl)
    if(NOT required IN_LIST libraries)
        message(FATAL_ERROR "Missing ${required} in Libs.private")
    endif()
endforeach()

function(require_order consumer provider)
    list(FIND libraries "${consumer}" consumer_index)
    list(FIND libraries "${provider}" provider_index)
    if(NOT consumer_index EQUAL -1 AND NOT provider_index EQUAL -1 AND
       NOT consumer_index LESS provider_index)
        message(FATAL_ERROR "${consumer} must precede ${provider}: ${libraries}")
    endif()
endfunction()

function(require_optional option library)
    list(FIND libraries "${library}" index)
    if(${option})
        if(index EQUAL -1)
            message(FATAL_ERROR "${option} requires ${library}")
        endif()
    elseif(NOT index EQUAL -1)
        message(FATAL_ERROR "${option}=OFF must not require ${library}")
    endif()
endfunction()

require_optional(WITH_GLOG -lglog)
require_optional(WITH_THRIFT -lthrift)
# Snappy can also be a dependency of the selected LevelDB package.
if(WITH_SNAPPY AND NOT "-lsnappy" IN_LIST libraries)
    message(FATAL_ERROR "WITH_SNAPPY requires -lsnappy")
endif()
require_optional(WITH_RDMA -libverbs)
require_optional(WITH_MESALINK -lmesalink)
require_order(-lglog -lgflags)
require_order(-lprotoc -lprotobuf)
require_order(-lleveldb -lsnappy)
require_order(-lssl -lcrypto)
require_order(-lmesalink -lcrypto)
require_order(-lssl -lz)
require_order(-lcrypto -lz)
require_order(-lprotobuf -lz)
require_order(-lthrift -lssl)
require_order(-labsl_stacktrace -lunwind)
require_order(-labsl_malloc_internal -labsl_base)
require_order(-labsl_base -labsl_spinlock_wait)
require_order(-labsl_base -labsl_raw_logging_internal)
require_order(-lunwind-x86_64 -lunwind)
require_order(-lunwind -ldl)
