if [ -z "$BASH" ]; then
    ECHO=echo
else
    ECHO='echo -e'
fi
# NOTE: This requires GNU getopt.  On Mac OS X and FreeBSD, you have to install this
# separately; see below.
TEMP=`getopt -o v: --long headers:,libs:,cc:,cxx: -n 'config_brpc' -- "$@"`

if [ $? != 0 ] ; then >&2 $ECHO "Terminating..."; exit 1 ; fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

# Convert to abspath always so that generated mk is include-able from everywhere
CC=gcc
CXX=g++
while true; do
    case "$1" in
        --headers ) HDRS="$(readlink -f $2)"; shift 2 ;;
        --libs ) LIBS="$(readlink -f $2)"; shift 2 ;;
        --cc ) CC=$2; shift 2 ;;
        --cxx ) CXX=$2; shift 2 ;;
        -- ) shift; break ;;
        * ) break ;;
    esac
done

if [ -z "$HDRS" ] || [ -z "$LIBS" ]; then
    >&2 $ECHO "config_brpc: --headers=HDRPATHS --libs=LIBPATHS must be specified"
    exit 1
fi

find_dir_of_lib() {
    local lib=$(find ${LIBS} -name "lib${1}.a" -o -name "lib${1}.so*" | head -n1)
    if [ ! -z "$lib" ]; then
        dirname $lib
    fi
}
find_dir_of_lib_or_die() {
    local dir=$(find_dir_of_lib $1)
    if [ -z "$dir" ]; then
        >&2 $ECHO "fail to find $1 from -libs"
        exit 1
    else
        $ECHO $dir
    fi
}

find_bin() {
    find ${LIBS} -name "$1" | head -n1
}

find_dir_of_header() {
    find ${HDRS} -path "*/$1" | head -n1 | sed "s|$1||g"
}
find_dir_of_header_or_die() {
    local dir=$(find_dir_of_header $1)
    if [ -z "$dir" ]; then
        >&2 $ECHO "fail to find $1 from -incs"
        exit 1
    else
        $ECHO $dir
    fi
}

STATIC_LINKINGS=
DYNAMIC_LINKINGS="-lpthread -lrt -lssl -lcrypto -ldl -lz"
append_linking() {
    if [ -f $1/lib${2}.a ]; then
        STATIC_LINKINGS="${STATIC_LINKINGS} -l$2"
    else
        DYNAMIC_LINKINGS="${DYNAMIC_LINKINGS} -l$2"
    fi
}

GFLAGS_LIB=$(find_dir_of_lib_or_die gflags)
append_linking $GFLAGS_LIB gflags
PROTOBUF_LIB=$(find_dir_of_lib_or_die protobuf)
append_linking $PROTOBUF_LIB protobuf
PROTOC_LIB=$(find_dir_of_lib_or_die protoc)
LEVELDB_LIB=$(find_dir_of_lib_or_die leveldb)
append_linking $LEVELDB_LIB leveldb
# required by leveldb
SNAPPY_LIB=$(find_dir_of_lib snappy)
if [ ! -z "$SNAPPY_LIB" ]; then
    append_linking $SNAPPY_LIB snappy
fi

PROTOC=$(which protoc 2>/dev/null)
if [ -z "$PROTOC" ]; then
    PROTOC=$(find_bin protoc)
    if [ -z "$PROTOC" ]; then
        >&2 $ECHO "Fail to find protoc"
        exit 1
    fi
fi

GFLAGS_HDR=$(find_dir_of_header gflags/gflags.h)
PROTOBUF_HDR=$(find_dir_of_header google/protobuf/message.h)
LEVELDB_HDR=$(find_dir_of_header leveldb/db.h)

HDRS2=$($ECHO "$GFLAGS_HDR\n$PROTOBUF_HDR\n$LEVELDB_HDR" | sort | uniq)
LIBS2=$($ECHO "$GFLAGS_LIB\n$PROTOBUF_LIB\n$LEVELDB_LIB\n$SNAPPY_LIB\n$PROTOC_LIB" | sort | uniq)

absent_in_the_list() {
    TMP=$($ECHO "`$ECHO "$1\n$2" | sort | uniq`")
    if [ "${TMP}" = "$2" ]; then
        return 1
    fi
    return 0
}

#can't use \n in texts because sh does not support -e
CONTENT="HDRS=$($ECHO $HDRS2)"
CONTENT="${CONTENT}\nLIBS=$($ECHO $LIBS2)"
CONTENT="${CONTENT}\nPROTOC=$PROTOC"
CONTENT="${CONTENT}\nPROTOBUF_HDR=$PROTOBUF_HDR"
CONTENT="${CONTENT}\nCC=$CC"
CONTENT="${CONTENT}\nCXX=$CXX"
CONTENT="${CONTENT}\nSTATIC_LINKINGS=$STATIC_LINKINGS"
CONTENT="${CONTENT}\nDYNAMIC_LINKINGS=$DYNAMIC_LINKINGS"
CONTENT="${CONTENT}\nifeq (\$(NEED_GPERFTOOLS), 1)"
# required by cpu/heap profiler
TCMALLOC_LIB=$(find_dir_of_lib tcmalloc_and_profiler)
if [ -z "$TCMALLOC_LIB" ]; then
    CONTENT="${CONTENT}\n    \$(error \"Fail to find gperftools\")"
else
    if absent_in_the_list "$TCMALLOC_LIB" "$LIBS2"; then
        CONTENT="${CONTENT}\n    LIBS+=$TCMALLOC_LIB"
        LIBS2="${LIBS2}\n$TCMALLOC_LIB"
    fi
    TCMALLOC_HDR=$(find_dir_of_header google/tcmalloc.h)
    if absent_in_the_list "$TCMALLOC_HDR" "$HDRS2"; then
        CONTENT="${CONTENT}\n    HDRS+=$TCMALLOC_HDR"
        HDRS2="${HDRS2}\n$TCMALLOC_HDR"
    fi
    if [ -f $TCMALLOC_LIB/libtcmalloc_and_profiler.a ]; then
        CONTENT="${CONTENT}\n    STATIC_LINKINGS+=-ltcmalloc_and_profiler"
    else
        CONTENT="${CONTENT}\n    DYNAMIC_LINKINGS+=-ltcmalloc_and_profiler"
    fi
fi
# required by tcmalloc('s profiler)
UNWIND_LIB=$(find_dir_of_lib unwind)
if [ ! -z "$UNWIND_LIB" ]; then
    if absent_in_the_list "$UNWIND_LIB" "$LIBS2"; then
        CONTENT="${CONTENT}\n    LIBS+=$UNWIND_LIB"
        LIBS2="${LIBS2}\n$UNWIND_LIB"
    fi
    if [ -f $UNWIND_LIB/libunwind.a ]; then
        CONTENT="${CONTENT}\n    STATIC_LINKINGS+=-lunwind"
    else
        CONTENT="${CONTENT}\n    DYNAMIC_LINKINGS+=-lunwind"
    fi
    # required by libunwind
    CONTENT="${CONTENT}\n    DYNAMIC_LINKINGS+=-llzma"
fi
CONTENT="${CONTENT}\nendif"

# required by UT
#gtest
GTEST_LIB=$(find_dir_of_lib gtest)
CONTENT="${CONTENT}\nifeq (\$(NEED_GTEST), 1)"
if [ -z "$GTEST_LIB" ]; then
    CONTENT="${CONTENT}\n    \$(error \"Fail to find gtest\")"
else
    GTEST_HDR=$(find_dir_of_header gtest/gtest.h)
    if absent_in_the_list "$GTEST_LIB" "$LIBS2"; then
        CONTENT="${CONTENT}\n    LIBS+=$GTEST_LIB"
        LIBS2="${LIBS2}\n$GTEST_LIB"
    fi
    if absent_in_the_list "$GTEST_HDR" "$HDRS2"; then
        CONTENT="${CONTENT}\n    HDRS+=$GTEST_HDR"
        HDRS2="${HDRS2}\n$GTEST_HDR"
    fi
    if [ -f $GTEST_LIB/libgtest.a ]; then
        CONTENT="${CONTENT}\n    STATIC_LINKINGS+=-lgtest -lgtest_main"
    else
        CONTENT="${CONTENT}\n    DYNAMIC_LINKINGS+=-lgtest -lgtest_main"
    fi
fi
CONTENT="${CONTENT}\nendif"
#gmock
GMOCK_LIB=$(find_dir_of_lib gmock)
CONTENT="${CONTENT}\nifeq (\$(NEED_GMOCK), 1)"
if [ -z "$GMOCK_LIB" ]; then
    CONTENT="${CONTENT}\n    \$(error \"Fail to find gmock\")"
else
    GMOCK_HDR=$(find_dir_of_header gmock/gmock.h)
    if absent_in_the_list "$GMOCK_LIB" "$LIBS2"; then
        CONTENT="${CONTENT}\n    LIBS+=$GMOCK_LIB"
        LIBS2="${LIBS2}\n$GMOCK_LIB"
    fi
    if absent_in_the_list "$GMOCK_HDR" "$HDRS2"; then
        CONTENT="${CONTENT}\n    HDRS+=$GMOCK_HDR"
        HDRS2="${HDRS2}\n$GMOCK_HDR"
    fi
    if [ -f $GMOCK_LIB/libgmock.a ]; then
        CONTENT="${CONTENT}\n    STATIC_LINKINGS+=-lgmock -lgmock_main"
    else
        CONTENT="${CONTENT}\n    DYNAMIC_LINKINGS+=-lgmock -lgmock_main"
    fi
fi
CONTENT="${CONTENT}\nendif"
$ECHO "$CONTENT" > config.mk
