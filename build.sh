#!/bin/bash

export PATH=/opt/compiler/gcc-8.2/bin:$PATH

sh ./config_brpc.sh --headers="/home/users/huxiguo/brpc-dep/include" --libs="/home/users/huxiguo/brpc-dep/lib"

make -j40
