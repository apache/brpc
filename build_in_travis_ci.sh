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

if [ "$PURPOSE" = "compile-with-bazel" ]; then
    runcmd "bazel build -j 12 -c opt --copt -DHAVE_ZLIB=1 //..."
    exit 0
fi

# The default env in travis-ci is Ubuntu.
if ! sh config_brpc.sh --headers=/usr/include --libs=/usr/lib --nodebugsymbols --cxx=$CXX --cc=$CC; then
    echo "Fail to configure brpc"
    exit 1
fi
if [ "$PURPOSE" = "compile" ]; then
    make -j4 && sh tools/make_all_examples
elif [ "$PURPOSE" = "unittest" ]; then
    # pass the unittest from default Makefile to accelerate build process
    :
else
    echo "Unknown purpose=\"$PURPOSE\""
fi

echo "start building by cmake"
rm -rf build && mkdir build && cd build
if [ "$PURPOSE" = "compile" ]; then
    if ! cmake ..; then
        echo "Fail to generate Makefile by cmake"
        exit 1
    fi
    make -j4
elif [ "$PURPOSE" = "unittest" ]; then
    if ! cmake -DBUILD_UNIT_TESTS=ON ..; then
        echo "Fail to generate Makefile by cmake"
        exit 1
    fi
    make -j4 && cd test && sh ./run_tests.sh && cd ../
else
    echo "Unknown purpose=\"$PURPOSE\""
fi
