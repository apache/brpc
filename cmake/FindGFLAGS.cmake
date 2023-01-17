# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(_gflags_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})

find_path(GFLAGS_INCLUDE_PATH gflags/gflags.h)

if (GFLAGS_STATIC)
  if (WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
  else (WIN32)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  endif (WIN32)
endif (GFLAGS_STATIC)
find_library(GFLAGS_LIBRARY NAMES gflags libgflags)
if(GFLAGS_INCLUDE_PATH AND GFLAGS_LIBRARY)
  set(GFLAGS_FOUND TRUE)
endif(GFLAGS_INCLUDE_PATH AND GFLAGS_LIBRARY)
if(GFLAGS_FOUND)
  if(NOT GFLAGS_FIND_QUIETLY)
    message(STATUS "Found gflags: ${GFLAGS_LIBRARY}")
  endif(NOT GFLAGS_FIND_QUIETLY)
else(GFLAGS_FOUND)
  if(GFLAGS_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find gflags library.")
  endif(GFLAGS_FIND_REQUIRED)
endif(GFLAGS_FOUND)

set(CMAKE_FIND_LIBRARY_SUFFIXES ${_gflags_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
