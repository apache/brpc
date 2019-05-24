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
EXTRA_BUILD_OPTS=""
if [ "$USE_MESALINK" = "yes" ]; then
    EXTRA_BUILD_OPTS="$EXTRA_BUILD_OPTS --with-mesalink"
fi
# The default env in travis-ci is Ubuntu.
if ! sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --nodebugsymbols --cxx=$CXX --cc=$CC $EXTRA_BUILD_OPTS; then
    echo "Fail to configure brpc"
    exit 1
fi
}

if [ "$PURPOSE" = "compile" ]; then
    init_make_config
    make -j4 && sh tools/make_all_examples
elif [ "$PURPOSE" = "unittest" ]; then
    init_make_config
    cd test
    make -j4 && sh ./run_tests.sh
elif [ "$PURPOSE" = "compile-with-bazel" ]; then
    bazel build -j 12 -c opt --copt -DHAVE_ZLIB=1 //...
elif [ "$PURPOSE" = "compile-with-cmake" ]; then
    rm -rf bld && mkdir bld && cd bld && cmake .. && make -j4
else
    echo "Unknown purpose=\"$PURPOSE\""
fi
