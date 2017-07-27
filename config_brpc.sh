# NOTE: This requires GNU getopt.  On Mac OS X and FreeBSD, you have to install this
# separately; see below.
TEMP=`getopt -o vdm: --long verbose:,gflags:,protobuf:,leveldb:,tcmalloc: -n 'config_brpc' -- "$@"`

if [ $? != 0 ] ; then echo "Terminating..." >&2 ; exit 1 ; fi

# Note the quotes around `$TEMP': they are essential!
eval set -- "$TEMP"

# Convert to abspath always so that generated mk is include-able from everywhere
VERBOSE=false
while true; do
    case "$1" in
        -v | --verbose ) VERBOSE=true; shift ;;
        --gflags ) GFLAGS_PATH="$(readlink -f $2)"; shift 2 ;;
        --protobuf ) PROTOBUF_PATH="$(readlink -f $2)"; shift 2 ;;
        --leveldb ) LEVELDB_PATH="$(readlink -f $2)"; shift 2 ;;
        --tcmalloc ) TCMALLOC_PATH="$(readlink -f $2)"; shift 2 ;;
        -- ) shift; break ;;
        * ) break ;;
    esac
done
if [[ -z "$GFLAGS_PATH" ]];then
    >&2 echo "config_brpc: --gflags=<GFLAGS_PATH> must be specified"
    exit 1
fi
if [[ -z "$PROTOBUF_PATH" ]];then
    >&2 echo "config_brpc: --protobuf=<PROTOBUF_PATH> must be specified"
    exit 1
fi
if [[ -z "$LEVELDB_PATH" ]];then
    >&2 echo "config_brpc: --leveldb=<LEVELDB_PATH> must be specified"
    exit 1
fi
CONTENT1="GFLAGS_PATH=$GFLAGS_PATH\nPROTOBUF_PATH=$PROTOBUF_PATH\nLEVELDB_PATH=$LEVELDB_PATH"
echo -e $CONTENT1 > config.mk

if [[ ! -z "$TCMALLOC_PATH" ]]; then
    CONTENT2="$CONTENT1\nTCMALLOC_PATH=$TCMALLOC_PATH"
else
    CONTENT2="\$(info TCMALLOC_PATH is not defined, run config_brpc.sh with --tcmalloc again)\nall:\n\texit 1"
fi
echo -e $CONTENT2 > config_with_tcmalloc.mk

