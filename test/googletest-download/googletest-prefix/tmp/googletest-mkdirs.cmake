# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/mike/桌面/brpc/test/googletest-src"
  "/home/mike/桌面/brpc/test/googletest-build"
  "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix"
  "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/tmp"
  "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/src/googletest-stamp"
  "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/src"
  "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/src/googletest-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/src/googletest-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/mike/桌面/brpc/test/googletest-download/googletest-prefix/src/googletest-stamp${cfgdir}") # cfgdir has leading slash
endif()
