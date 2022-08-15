#!/usr/bin/env sh

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

if [ -z "$PURPOSE" ]; then
    echo "PURPOSE must be set"
    exit 1
fi
if [ -z "$CXX" ]; then
    echo "CXX must be set"
    exit 1
fi
if [ -z "$CC" ]; then
    echo "CC must be set"
    exit 1
fi

runcmd(){
    eval $@
    [[ $? != 0 ]] && {
        exit 1
    }
    return 0
}

echo "build combination: PURPOSE=$PURPOSE CXX=$CXX CC=$CC"

init_make_config() {
    # The default env in travis-ci is Ubuntu.
    if ! sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --nodebugsymbols --cxx=$CXX --cc=$CC $@ ; then
        echo "Fail to configure brpc"
        exit 1
    fi
}

if [ "$PURPOSE" = "compile-with-make" ]; then
    # In order to run thrift example, we need to add the corresponding flag
    init_make_config "--with-thrift" && make -j4 && sh tools/make_all_examples
elif [ "$PURPOSE" = "unittest" ]; then
    init_make_config && cd test && make -j4 && sh ./run_tests.sh
elif [ "$PURPOSE" = "compile-with-cmake" ]; then
    rm -rf bld && mkdir bld && cd bld && cmake .. && make -j4
elif [ "$PURPOSE" = "compile-with-bazel" ]; then
    bazel build -j 12 -c opt --copt -DHAVE_ZLIB=1 //...
elif [ "$PURPOSE" = "compile-with-make-all-options" ]; then
    init_make_config "--with-thrift --with-glog" && make -j4
elif [ "$PURPOSE" = "compile-with-cmake-all-options" ]; then
    rm -rf bld && mkdir bld && cd bld && cmake -DWITH_MESALINK=OFF -DWITH_GLOG=ON -DWITH_THRIFT=ON .. && make -j4
elif [ "$PURPOSE" = "compile-with-bazel-all-options" ]; then
    bazel build -j 12 -c opt --define with_mesalink=false --define with_glog=true --define with_thrift=true --copt -DHAVE_ZLIB=1 //...
else
    echo "Unknown purpose=\"$PURPOSE\""
fi
