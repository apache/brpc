# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Usage of this module as follows:
#
# find_package(BORINGSSL)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
# BORINGSSL_ROOT_DIR          Set this variable to the root installation of
# boringssl if the module has problems finding the
# proper installation path.
#
# Variables defined by this module:
#
# BORINGSSL_FOUND             System has boringssl, include and library dirs found
# BORINGSSL_INCLUDE_DIR       The boringssl include directories.
# BORINGSSL_LIBRARIES         The boringssl libraries.
# BORINGSSL_CRYPTO_LIBRARY    The boringssl crypto library.
# BORINGSSL_SSL_LIBRARY       The boringssl ssl library.
# BORING_USE_STATIC_LIBS      Whether use static library.

if(BORING_USE_STATIC_LIBS)
    set(_boringssl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    if(MSVC)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .lib ${CMAKE_FIND_LIBRARY_SUFFIXES})
    else()
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
endif()

find_path(BORINGSSL_ROOT_DIR
          NAMES include/openssl/ssl.h include/openssl/base.h include/openssl/hkdf.h
          HINTS ${BORINGSSL_ROOT_DIR})

find_path(BORINGSSL_INCLUDE_DIR
          NAMES openssl/ssl.h openssl/base.h openssl/hkdf.h
          HINTS ${BORINGSSL_ROOT_DIR}/include)

find_library(BORINGSSL_SSL_LIBRARY
            NAMES ssl
            HINTS ${BORINGSSL_ROOT_DIR}/lib)

find_library(BORINGSSL_CRYPTO_LIBRARY
             NAMES crypto
             HINTS ${BORINGSSL_ROOT_DIR}/lib)

set(BORINGSSL_LIBRARIES ${BORINGSSL_SSL_LIBRARY} ${BORINGSSL_CRYPTO_LIBRARY}
    CACHE STRING "BoringSSL SSL and crypto libraries" FORCE)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(BoringSSL DEFAULT_MSG
                                  BORINGSSL_LIBRARIES
                                  BORINGSSL_INCLUDE_DIR)

mark_as_advanced(
        BORINGSSL_ROOT_DIR
        BORINGSSL_INCLUDE_DIR
        BORINGSSL_LIBRARIES
        BORINGSSL_CRYPTO_LIBRARY
        BORINGSSL_SSL_LIBRARY
)

set(CMAKE_FIND_LIBRARY_SUFFIXES ${_boringssl_ORIG_CMAKE_FIND_LIBRARY_SUFFIXES})
