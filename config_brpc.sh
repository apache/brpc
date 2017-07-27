#!/bin/bash
# NOTE: This requires GNU getopt.  On Mac OS X and FreeBSD, you have to install this
# separately; see below.
TEMP=`getopt -o v: --long incs:,libs:,cc:,cxx: -n 'config_brpc' -- "$@"`

if [ $? != 0 ] ; then echo "Terminating..." >&2 ; exit 1 ; fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

# Convert to abspath always so that generated mk is include-able from everywhere
CC=gcc
CXX=g++
while true; do
    case "$1" in
        --incs ) INCS="$(readlink -f $2)"; shift 2 ;;
        --libs ) LIBS="$(readlink -f $2)"; shift 2 ;;
        --cc ) CC=$2; shift 2 ;;
        --cxx ) CXX=$2; shift 2 ;;
        -- ) shift; break ;;
        * ) break ;;
    esac
done
if [ -z "$INCS" ] || [ -z "$LIBS" ]; then
    >&2 echo "config_brpc: --incs=INCPATHS --libs=LIBPATHS must be specified"
    exit 1
fi

find_lib() {
    find ${LIBS} -name "$1" | head -n1
}
find_lib_or_die() {
    local lib=$(find_lib $1)
    if [ -z "$lib" ]; then
        >&2 echo "fail to find $1 from -libs"
        exit 1
    else
        echo $lib
    fi
}
find_dir_of_header() {
    find ${INCS} -path "*/$1" | sed "s|$1||g"
}
find_dir_of_header_or_die() {
    local dir=$(find_dir_of_header $1)
    if [ -z "$dir" ]; then
        >&2 echo "fail to find $1 from -incs"
        exit 1
    else
        echo $dir
    fi
}

GFLAGS_LIB=$(find_lib_or_die libgflags.a)
PROTOBUF_LIB=$(find_lib_or_die libprotobuf.a)
PROTOC_LIB=$(find_lib_or_die libprotoc.a)
PROTOC=$(which protoc 2>/dev/null)
if [ -z "$PROTOC" ]; then
    PROTOC=$(find_lib_or_die protoc)
fi
LEVELDB_LIB=$(find_lib_or_die libleveldb.a)
SNAPPY_LIB=$(find_lib libsnappy.a)

GFLAGS_INC=$(find_dir_of_header gflags/gflags.h)
PROTOBUF_INC=$(find_dir_of_header google/protobuf/message.h)
LEVELDB_INC=$(find_dir_of_header leveldb/db.h)
NEW_INCS=$(echo $GFLAGS_INC $PROTOBUF_INC $LEVELDB_INC | sort | uniq)

#can't use \n in texts because sh does not support -e
echo "INCS=$NEW_INCS" > config.mk
echo "LIBS=$GFLAGS_LIB $PROTOBUF_LIB $LEVELDB_LIB $SNAPPY_LIB" >> config.mk
echo "PROTOC_LIB=$PROTOC_LIB" >> config.mk
echo "PROTOC=$PROTOC" >> config.mk
echo "PROTOBUF_INC=$PROTOBUF_INC" >> config.mk
echo "CC=$CC" >> config.mk
echo "CXX=$CXX" >> config.mk
echo "ifeq (\$(LINK_PERFTOOLS), 1)" >> config.mk
TCMALLOC_LIB=$(find_lib libtcmalloc_and_profiler.a)
if [ ! -z "$TCMALLOC_LIB" ]; then
    echo "    LIBS+=$TCMALLOC_LIB" >> config.mk
    TCMALLOC_INC=$(find_dir_of_header google/tcmalloc.h)
    if [ ! -z "$TCMALLOC_INC" ] && [ "$TCMALLOC_INC" != "$INCS" ]; then
        echo "    INCS+=$TCMALLOC_INC" >> config.mk
    fi
fi
UNWIND_LIB=$(find_lib libunwind.a)
if [ ! -z "$UNWIND_LIB" ]; then
    echo "    LIBS+=$UNWIND_LIB" >> config.mk
fi
echo "endif" >> config.mk
