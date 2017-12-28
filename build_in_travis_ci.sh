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
echo "build combination: PURPOSE=$PURPOSE CXX=$CXX CC=$CC"

if [ "$PURPOSE" = "compile-with-bazel" ]; then
    bazel build -j8 --copt -DHAVE_ZLIB=1 //...
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
    cd test && sh ./run_tests.sh
else
    echo "Unknown purpose=\"$PURPOSE\""
fi
