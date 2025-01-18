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

TEMP=`getopt -o v: --long headers:,libs:,cc:,cxx:,with-glog,with-thrift,with-rdma,with-mesalink,with-bthread-tracer,with-debug-bthread-sche-safety,with-debug-lock,nodebugsymbols,werror -n 'config_brpc' -- "$@"`
WITH_GLOG=0
WITH_THRIFT=0
WITH_RDMA=0
WITH_MESALINK=0
WITH_BTHREAD_TRACER=0
BRPC_DEBUG_BTHREAD_SCHE_SAFETY=0
DEBUGSYMBOLS=-g
WERROR=
BRPC_DEBUG_LOCK=0

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
        --with-rdma) WITH_RDMA=1; shift 1 ;;
        --with-mesalink) WITH_MESALINK=1; shift 1 ;;
        --with-bthread-tracer) WITH_BTHREAD_TRACER=1; shift 1 ;;
        --with-debug-bthread-sche-safety ) BRPC_DEBUG_BTHREAD_SCHE_SAFETY=1; shift 1 ;;
        --with-debug-lock ) BRPC_DEBUG_LOCK=1; shift 1 ;;
        --nodebugsymbols ) DEBUGSYMBOLS=; shift 1 ;;
        --werror ) WERROR=-Werror; shift 1 ;;
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

GCC_VERSION=$(CXX=$CXX tools/print_gcc_version.sh)
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
    if [ -d "/usr/local/opt/openssl" ]; then
        LIBS_IN="/usr/local/opt/openssl/lib $LIBS_IN"
        HDRS_IN="/usr/local/opt/openssl/include $HDRS_IN"
    elif [ -d "/opt/homebrew/Cellar" ]; then
        LIBS_IN="/opt/homebrew/Cellar $LIBS_IN"
        HDRS_IN="/opt/homebrew/Cellar $HDRS_IN"
    fi
fi

# User specified path of openssl, if not given it's empty
OPENSSL_LIB=$(find_dir_of_lib ssl)
# Inconvenient to check these headers in baidu-internal
#PTHREAD_HDR=$(find_dir_of_header_or_die pthread.h)
OPENSSL_HDR=$(find_dir_of_header_or_die openssl/ssl.h mesalink/openssl/ssl.h)

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
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,__Z13GetStackTracePPvii"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_RegisterThriftProtocol"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_mallctl"
	DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -Wl,-U,_malloc_stats_print"
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

PROTOBUF_HDR=$(find_dir_of_header_or_die google/protobuf/message.h)
PROTOBUF_VERSION=$(grep '#define GOOGLE_PROTOBUF_VERSION [0-9]\+' $PROTOBUF_HDR/google/protobuf/stubs/common.h | awk '{print $3}')
if [ "$PROTOBUF_VERSION" -ge 4022000 ]; then
    ABSL_HDR=$(find_dir_of_header_or_die absl/base/config.h)
    ABSL_LIB=$(find_dir_of_lib_or_die absl_strings)
    ABSL_TARGET_NAMES="
        absl_bad_optional_access
        absl_bad_variant_access
        absl_base
        absl_city
        absl_civil_time
        absl_cord
        absl_cord_internal
        absl_cordz_functions
        absl_cordz_handle
        absl_cordz_info
        absl_crc32c
        absl_crc_cord_state
        absl_crc_cpu_detect
        absl_crc_internal
        absl_debugging_internal
        absl_demangle_internal
        absl_die_if_null
        absl_examine_stack
        absl_exponential_biased
        absl_flags
        absl_flags_commandlineflag
        absl_flags_commandlineflag_internal
        absl_flags_config
        absl_flags_internal
        absl_flags_marshalling
        absl_flags_private_handle_accessor
        absl_flags_program_name
        absl_flags_reflection
        absl_graphcycles_internal
        absl_hash
        absl_hashtablez_sampler
        absl_int128
        absl_kernel_timeout_internal
        absl_leak_check
        absl_log_entry
        absl_log_globals
        absl_log_initialize
        absl_log_internal_check_op
        absl_log_internal_conditions
        absl_log_internal_format
        absl_log_internal_globals
        absl_log_internal_log_sink_set
        absl_log_internal_message
        absl_log_internal_nullguard
        absl_log_internal_proto
        absl_log_severity
        absl_log_sink
        absl_low_level_hash
        absl_malloc_internal
        absl_raw_hash_set
        absl_raw_logging_internal
        absl_spinlock_wait
        absl_stacktrace
        absl_status
        absl_statusor
        absl_str_format_internal
        absl_strerror
        absl_string_view
        absl_strings
        absl_strings_internal
        absl_symbolize
        absl_synchronization
        absl_throw_delegate
        absl_time
        absl_time_zone
    "
    for i in $ABSL_TARGET_NAMES; do
        # ignore interface targets
        if [ -n "$(find_dir_of_lib $i)" ]; then
            append_linking "$ABSL_LIB" "$i"
        fi
    done
    CXXFLAGS="-std=c++17"
else
    CXXFLAGS="-std=c++0x"
fi

LEVELDB_HDR=$(find_dir_of_header_or_die leveldb/db.h)

CPPFLAGS=

if [ $WITH_BTHREAD_TRACER != 0 ]; then
    if [ "$SYSTEM" != "Linux" ] || [ "$(uname -m)" != "x86_64" ]; then
        >&2 $ECHO "bthread tracer is only supported on Linux x86_64 platform"
        exit 1
    fi
    LIBUNWIND_HDR=$(find_dir_of_header_or_die libunwind.h)
    LIBUNWIND_LIB=$(find_dir_of_lib_or_die unwind)

    CPPFLAGS="${CPPFLAGS} -DBRPC_BTHREAD_TRACER"

    if [ -f "$LIBUNWIND_LIB/libunwind.$SO" ]; then
        DYNAMIC_LINKINGS="$DYNAMIC_LINKINGS -lunwind -lunwind-x86_64"
    else
        STATIC_LINKINGS="$STATIC_LINKINGS -lunwind -lunwind-x86_64"
    fi
fi

HDRS=$($ECHO "$LIBUNWIND_HDR\n$GFLAGS_HDR\n$PROTOBUF_HDR\n$ABSL_HDR\n$LEVELDB_HDR\n$OPENSSL_HDR" | sort | uniq)
LIBS=$($ECHO "$LIBUNWIND_LIB\n$GFLAGS_LIB\n$PROTOBUF_LIB\n$ABSL_LIB\n$LEVELDB_LIB\n$OPENSSL_LIB\n$SNAPPY_LIB" | sort | uniq)

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

# CPP means C PreProcessing, not C PlusPlus
CPPFLAGS="${CPPFLAGS}  -DBRPC_WITH_GLOG=$WITH_GLOG -DBRPC_DEBUG_BTHREAD_SCHE_SAFETY=$BRPC_DEBUG_BTHREAD_SCHE_SAFETY -DBRPC_DEBUG_LOCK=$BRPC_DEBUG_LOCK"

# Avoid over-optimizations of TLS variables by GCC>=4.8
# See: https://github.com/apache/brpc/issues/1693
CPPFLAGS="${CPPFLAGS} -D__const__=__unused__"

if [ ! -z "$DEBUGSYMBOLS" ]; then
    CPPFLAGS="${CPPFLAGS} $DEBUGSYMBOLS"
fi
if [ ! -z "$WERROR" ]; then
    CPPFLAGS="${CPPFLAGS} $WERROR"
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
    # get thrift version
    thrift_version=$(thrift --version | awk '{print $3}')
    major=$(echo "$thrift_version" | awk -F '.' '{print $1}')
    minor=$(echo "$thrift_version" | awk -F '.' '{print $2}')
    if [ $((major)) -eq 0 -a $((minor)) -lt 11 ]; then
        CPPFLAGS="${CPPFLAGS} -D_THRIFT_VERSION_LOWER_THAN_0_11_0_"
        echo "less"
    else
        echo "greater"
    fi
fi

if [ $WITH_RDMA != 0 ]; then
    RDMA_LIB=$(find_dir_of_lib_or_die ibverbs)
    RDMA_HDR=$(find_dir_of_header_or_die infiniband/verbs.h)
    append_to_output_libs "$RDMA_LIB"
    append_to_output_headers "$RDMA_HDR"

    CPPFLAGS="${CPPFLAGS} -DBRPC_WITH_RDMA"

    append_to_output "DYNAMIC_LINKINGS+=-libverbs"
    append_to_output "WITH_RDMA=1"
fi

if [ $WITH_MESALINK != 0 ]; then
    CPPFLAGS="${CPPFLAGS} -DUSE_MESALINK"
fi

append_to_output "CPPFLAGS=${CPPFLAGS}"
append_to_output "# without the flag, linux+arm64 may crash due to folding on TLS.
ifeq (\$(CC),gcc)
  ifeq (\$(shell uname -p),aarch64) 
    CPPFLAGS+=-fno-gcse
  endif
endif
"

append_to_output "CXXFLAGS=${CXXFLAGS}"

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
