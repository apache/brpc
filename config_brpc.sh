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

SYSTEM=$(uname -s)
if [ "$SYSTEM" = "Darwin" ]; then
    if [ -z "$BASH" ] || [ "$BASH" = "/bin/sh" ] ; then
        ECHO=echo
    else
        ECHO='echo -e'
    fi
    SO=dylib
    LDD="otool -L"
    if [ "$(getopt -V)" = " --" ]; then
        >&2 $ECHO "gnu-getopt must be installed and used"
        exit 1
    fi
else
    if [ -z "$BASH" ]; then
        ECHO=echo
    else
        ECHO='echo -e'
    fi
    SO=so
    LDD=ldd
fi

TEMP=`getopt -o v: --long headers:,libs:,cc:,cxx:,with-glog,with-thrift,with-mesalink,nodebugsymbols -n 'config_brpc' -- "$@"`
WITH_GLOG=0
WITH_THRIFT=0
WITH_MESALINK=0
DEBUGSYMBOLS=-g

if [ $? != 0 ] ; then >&2 $ECHO "Terminating..."; exit 1 ; fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

if [ "$SYSTEM" = "Darwin" ]; then
    REALPATH=realpath
else
    REALPATH="readlink -f"
fi

# Convert to abspath always so that generated mk is include-able from everywhere
while true; do
    case "$1" in
        --headers ) HDRS_IN="$(${REALPATH} $2)"; shift 2 ;;
        --libs ) LIBS_IN="$(${REALPATH} $2)"; shift 2 ;;
        --cc ) CC=$2; shift 2 ;;
        --cxx ) CXX=$2; shift 2 ;;
        --with-glog ) WITH_GLOG=1; shift 1 ;;
        --with-thrift) WITH_THRIFT=1; shift 1 ;;
        --with-mesalink) WITH_MESALINK=1; shift 1 ;;
        --nodebugsymbols ) DEBUGSYMBOLS=; shift 1 ;;
        -- ) shift; break ;;
        * ) break ;;
    esac
done

if [ -z "$CC" ]; then
    if [ ! -z "$CXX" ]; then
        >&2 $ECHO "--cc and --cxx must be both set or unset"
        exit 1
    fi
    CC=gcc
    CXX=g++
    if [ "$SYSTEM" = "Darwin" ]; then
        CC=clang
        CXX=clang++
    fi
elif [ -z "$CXX" ]; then
    >&2 $ECHO "--cc and --cxx must be both set or unset"
    exit 1
fi

GCC_VERSION=$($CXX tools/print_gcc_version.cc -o print_gcc_version && ./print_gcc_version && rm ./print_gcc_version)
if [ $GCC_VERSION -gt 0 ] && [ $GCC_VERSION -lt 40800 ]; then
    >&2 $ECHO "GCC is too old, please install a newer version supporting C++11"
    exit 1
fi

if [ -z "$HDRS_IN" ] || [ -z "$LIBS_IN" ]; then
    >&2 $ECHO "config_brpc: --headers=HDRPATHS --libs=LIBPATHS must be specified"
    exit 1
fi

find_dir_of_lib() {
    local lib=$(find ${LIBS_IN} -name "lib${1}.a" -o -name "lib${1}.$SO" 2>/dev/null | head -n1)
    if [ ! -z "$lib" ]; then
        dirname $lib
    fi
}
find_dir_of_lib_or_die() {
    local dir=$(find_dir_of_lib $1)
    if [ -z "$dir" ]; then
        >&2 $ECHO "Fail to find $1 from --libs"
        exit 1
    else
        $ECHO $dir
    fi
}

find_bin() {
    TARGET_BIN=$(find -L ${LIBS_IN} -type f -name "$1" 2>/dev/null | head -n1)
    if [ ! -z "$TARGET_BIN" ]; then
        $ECHO $TARGET_BIN
    else
        which "$1" 2>/dev/null
    fi
}
find_bin_or_die() {
    TARGET_BIN=$(find_bin "$1")
    if [ ! -z "$TARGET_BIN" ]; then
        $ECHO $TARGET_BIN
    else
        >&2 $ECHO "Fail to find $1"
        exit 1
    fi
}

find_dir_of_header() {
    find -L ${HDRS_IN} -path "*/$1" | head -n1 | sed "s|$1||g"
}

find_dir_of_header_excluding() {
    find -L ${HDRS_IN} -path "*/$1" | grep -v "$2\$" | head -n1 | sed "s|$1||g"
}

find_dir_of_header_or_die() {
    if [ -z "$2" ]; then
        local dir=$(find_dir_of_header $1)
    else
        local dir=$(find_dir_of_header_excluding $1 $2)
    fi
    if [ -z "$dir" ]; then
        >&2 $ECHO "Fail to find $1 from --headers"
        exit 1
    fi
    $ECHO $dir
}

if [ "$SYSTEM" = "Darwin" ]; then
    OPENSSL_LIB="/usr/local/opt/openssl/lib"
    OPENSSL_HDR="/usr/local/opt/openssl/include"
else
    # User specified path of openssl, if not given it's empty
    OPENSSL_LIB=$(find_dir_of_lib ssl)
    # Inconvenient to check these headers in baidu-internal
    #PTHREAD_HDR=$(find_dir_of_header_or_die pthread.h)
    OPENSSL_HDR=$(find_dir_of_header_or_die openssl/ssl.h)
fi

if [ $WITH_MESALINK != 0 ]; then
    MESALINK_HDR=$(find_dir_of_header_or_die mesalink/openssl/ssl.h)
    OPENSSL_HDR="$OPENSSL_HDR\n$MESALINK_HDR"
fi

STATIC_LINKINGS=
DYNAMIC_LINKINGS="-lpthread -lssl -lcrypto -ldl -lz"

if [ $WITH_MESALINK != 0 ]; then
    DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -lmesalink"
fi

if [ "$SYSTEM" = "Linux" ]; then
    DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -lrt"
fi
if [ "$SYSTEM" = "Darwin" ]; then
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework CoreFoundation"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework CoreGraphics"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework CoreData"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework CoreText"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework Security"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -framework Foundation"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_MallocExtension_ReleaseFreeMemory"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_ProfilerStart"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_ProfilerStop"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_RegisterThriftProtocol"
fi
append_linking() {
    if [ -f $1/lib${2}.a ]; then
        if [ "$SYSTEM" = "Darwin" ]; then
            # *.a must be explicitly specified in clang
            STATIC_LINKINGS="$STATIC_LINKINGS $1/lib${2}.a"
        else
            STATIC_LINKINGS="$STATIC_LINKINGS -l$2"
        fi
        export STATICALLY_LINKED_$2=1
    else
        DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -l$2"
        export STATICALLY_LINKED_$2=0
    fi
}

GFLAGS_LIB=$(find_dir_of_lib_or_die gflags)
append_linking $GFLAGS_LIB gflags

PROTOBUF_LIB=$(find_dir_of_lib_or_die protobuf)
append_linking $PROTOBUF_LIB protobuf

LEVELDB_LIB=$(find_dir_of_lib_or_die leveldb)
# required by leveldb
if [ -f $LEVELDB_LIB/libleveldb.a ]; then
    if [ -f $LEVELDB_LIB/libleveldb.$SO ]; then
        if $LDD $LEVELDB_LIB/libleveldb.$SO | grep -q libsnappy; then
            SNAPPY_LIB=$(find_dir_of_lib snappy)
            REQUIRE_SNAPPY="yes"
        fi
    fi
    if [ -z "$REQUIRE_SNAPPY" ]; then
        if [ "$SYSTEM" = "Darwin" ]; then
	        STATIC_LINKINGS="$STATIC_LINKINGS $LEVELDB_LIB/libleveldb.a"
        else
	        STATIC_LINKINGS="$STATIC_LINKINGS -lleveldb"
        fi
    elif [ -f $SNAPPY_LIB/libsnappy.a ]; then
        if [ "$SYSTEM" = "Darwin" ]; then
	        STATIC_LINKINGS="$STATIC_LINKINGS $LEVELDB_LIB/libleveldb.a $SNAPPY_LIB/libsnappy.a"
        else
	        STATIC_LINKINGS="$STATIC_LINKINGS -lleveldb -lsnappy"
        fi
    else
	    DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -lleveldb"
    fi
else
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -lleveldb"
fi

PROTOC=$(find_bin_or_die protoc)

GFLAGS_HDR=$(find_dir_of_header_or_die gflags/gflags.h)
# namespace of gflags may not be google, grep it from source.
GFLAGS_NS=$(grep "namespace [_A-Za-z0-9]\+ {" $GFLAGS_HDR/gflags/gflags_declare.h | head -1 | awk '{print $2}')
if [ "$GFLAGS_NS" = "GFLAGS_NAMESPACE" ]; then
    GFLAGS_NS=$(grep "#define GFLAGS_NAMESPACE [_A-Za-z0-9]\+" $GFLAGS_HDR/gflags/gflags_declare.h | head -1 | awk '{print $3}')
fi
if [ -z "$GFLAGS_NS" ]; then
    >&2 $ECHO "Fail to grep namespace of gflags source $GFLAGS_HDR/gflags/gflags_declare.h"
    exit 1
fi

PROTOBUF_HDR=$(find_dir_of_header_or_die google/protobuf/message.h)
LEVELDB_HDR=$(find_dir_of_header_or_die leveldb/db.h)

HDRS=$($ECHO "$GFLAGS_HDR\n$PROTOBUF_HDR\n$LEVELDB_HDR\n$OPENSSL_HDR" | sort | uniq)
LIBS=$($ECHO "$GFLAGS_LIB\n$PROTOBUF_LIB\n$LEVELDB_LIB\n$OPENSSL_LIB\n$SNAPPY_LIB" | sort | uniq)

absent_in_the_list() {
    TMP=`$ECHO "$1\n$2" | sort | uniq`
    if [ "${TMP}" = "$2" ]; then
        return 1
    fi
    return 0
}

OUTPUT_CONTENT="# Generated by config_brpc.sh, don't modify manually"
append_to_output() {
    OUTPUT_CONTENT="${OUTPUT_CONTENT}\n$*"
}
# $1: libname, $2: indentation
append_to_output_headers() {
    if absent_in_the_list "$1" "$HDRS"; then
        append_to_output "${2}HDRS+=$1"
        HDRS=`$ECHO "${HDRS}\n$1" | sort | uniq`
    fi
}
# $1: libname, $2: indentation
append_to_output_libs() {
    if absent_in_the_list "$1" "$LIBS"; then
        append_to_output "${2}LIBS+=$1"
        LIBS=`$ECHO "${LIBS}\n$1" | sort | uniq`
    fi
}
# $1: libdir, $2: libname, $3: indentation
append_to_output_linkings() {
    if [ -f $1/lib$2.a ]; then
        append_to_output_libs $1 $3
        if [ "$SYSTEM" = "Darwin" ]; then
            append_to_output "${3}STATIC_LINKINGS+=$1/lib$2.a"
        else
            append_to_output "${3}STATIC_LINKINGS+=-l$2"
        fi
        export STATICALLY_LINKED_$2=1
    else
        append_to_output_libs $1 $3
        append_to_output "${3}DYNAMIC_LINKINGS+=-l$2"
        export STATICALLY_LINKED_$2=0
    fi
}

#can't use \n in texts because sh does not support -e
append_to_output "SYSTEM=$SYSTEM"
append_to_output "HDRS=$($ECHO $HDRS)"
append_to_output "LIBS=$($ECHO $LIBS)"
append_to_output "PROTOC=$PROTOC"
append_to_output "PROTOBUF_HDR=$PROTOBUF_HDR"
append_to_output "CC=$CC"
append_to_output "CXX=$CXX"
append_to_output "GCC_VERSION=$GCC_VERSION"
append_to_output "STATIC_LINKINGS=$STATIC_LINKINGS"
append_to_output "DYNAMIC_LINKINGS=$DYNAMIC_LINKINGS"
CPPFLAGS="-DBRPC_WITH_GLOG=$WITH_GLOG -DGFLAGS_NS=$GFLAGS_NS"

if [ ! -z "$DEBUGSYMBOLS" ]; then
    CPPFLAGS="${CPPFLAGS} $DEBUGSYMBOLS"
fi
if [ "$SYSTEM" = "Darwin" ]; then
    CPPFLAGS="${CPPFLAGS} -Wno-deprecated-declarations -Wno-inconsistent-missing-override"
    version=`sw_vers -productVersion | awk -F '.' '{print $1 "." $2}'`
    if [[ `echo "$version<10.12" | bc -l` == 1 ]]; then
        CPPFLAGS="${CPPFLAGS} -DNO_CLOCK_GETTIME_IN_MAC"
    fi
fi

if [ $WITH_THRIFT != 0 ]; then
    THRIFT_LIB=$(find_dir_of_lib_or_die thriftnb)
    THRIFT_HDR=$(find_dir_of_header_or_die thrift/Thrift.h)
    append_to_output_libs "$THRIFT_LIB"
    append_to_output_headers "$THRIFT_HDR"

    CPPFLAGS="${CPPFLAGS} -DENABLE_THRIFT_FRAMED_PROTOCOL"

    if [ -f "$THRIFT_LIB/libthriftnb.$SO" ]; then
        append_to_output "DYNAMIC_LINKINGS+=-lthriftnb -levent -lthrift"
    else
        append_to_output "STATIC_LINKINGS+=-lthriftnb"
    fi
fi

if [ $WITH_MESALINK != 0 ]; then
    CPPFLAGS="${CPPFLAGS} -DUSE_MESALINK"
fi

append_to_output "CPPFLAGS=${CPPFLAGS}"

append_to_output "ifeq (\$(NEED_LIBPROTOC), 1)"
PROTOC_LIB=$(find $PROTOBUF_LIB -name "libprotoc.*" | head -n1)
if [ -z "$PROTOC_LIB" ]; then
    append_to_output "   \$(error \"Fail to find libprotoc\")"
else
    # libprotobuf and libprotoc must be linked same statically or dynamically
    # otherwise the bin will crash.
    if [ $STATICALLY_LINKED_protobuf -gt 0 ]; then
        if [ "$SYSTEM" = "Darwin" ]; then
            append_to_output "    STATIC_LINKINGS+=$(find $PROTOBUF_LIB -name "libprotoc.a" | head -n1)"
        else
            append_to_output "    STATIC_LINKINGS+=-lprotoc"
        fi
    else
        append_to_output "    DYNAMIC_LINKINGS+=-lprotoc"
    fi
fi
append_to_output "endif"

OLD_HDRS=$HDRS
OLD_LIBS=$LIBS
append_to_output "ifeq (\$(NEED_GPERFTOOLS), 1)"
# required by cpu/heap profiler
TCMALLOC_LIB=$(find_dir_of_lib tcmalloc_and_profiler)
if [ -z "$TCMALLOC_LIB" ]; then
    append_to_output "    \$(error \"Fail to find gperftools\")"
else
    append_to_output_libs "$TCMALLOC_LIB" "    "
    if [ -f $TCMALLOC_LIB/libtcmalloc.$SO ]; then
        append_to_output "    DYNAMIC_LINKINGS+=-ltcmalloc_and_profiler"
    else
        if [ "$SYSTEM" = "Darwin" ]; then
            append_to_output "    STATIC_LINKINGS+=$TCMALLOC_LIB/libtcmalloc.a"
        else
            append_to_output "    STATIC_LINKINGS+=-ltcmalloc_and_profiler"
        fi
    fi
fi
append_to_output "endif"

if [ $WITH_GLOG != 0 ]; then
    GLOG_LIB=$(find_dir_of_lib_or_die glog)
    GLOG_HDR=$(find_dir_of_header_or_die glog/logging.h windows/glog/logging.h)
    append_to_output_libs "$GLOG_LIB"
    append_to_output_headers "$GLOG_HDR"
    if [ -f "$GLOG_LIB/libglog.$SO" ]; then
        append_to_output "DYNAMIC_LINKINGS+=-lglog"
    else
        if [ "$SYSTEM" = "Darwin" ]; then
            append_to_output "STATIC_LINKINGS+=$GLOG_LIB/libglog.a"
        else
            append_to_output "STATIC_LINKINGS+=-lglog"
        fi
    fi
fi

# required by UT
#gtest
GTEST_LIB=$(find_dir_of_lib gtest)
HDRS=$OLD_HDRS
LIBS=$OLD_LIBS
append_to_output "ifeq (\$(NEED_GTEST), 1)"
if [ -z "$GTEST_LIB" ]; then
    append_to_output "    \$(error \"Fail to find gtest\")"
else
    GTEST_HDR=$(find_dir_of_header_or_die gtest/gtest.h)
    append_to_output_libs $GTEST_LIB "    "
    append_to_output_headers $GTEST_HDR "    "
    append_to_output_linkings $GTEST_LIB gtest "    "
    append_to_output_linkings $GTEST_LIB gtest_main "    "
fi
append_to_output "endif"

# generate src/butil/config.h
cat << EOF > src/butil/config.h
// This file is auto-generated by $(basename "$0"). DON'T edit it!
#ifndef  BUTIL_CONFIG_H
#define  BUTIL_CONFIG_H

#ifdef BRPC_WITH_GLOG
#undef BRPC_WITH_GLOG
#endif
#define BRPC_WITH_GLOG $WITH_GLOG

#endif  // BUTIL_CONFIG_H
EOF

# write to config.mk
$ECHO "$OUTPUT_CONTENT" > config.mk
