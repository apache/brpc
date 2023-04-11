[中文版](../cn/getting_started.md)

# BUILD

brpc prefers static linkages of deps, so that they don't have to be installed on every machine running the app.

brpc depends on following packages:

* [gflags](https://github.com/gflags/gflags): Extensively used to define global options.
* [protobuf](https://github.com/google/protobuf): Serializations of messages, interfaces of services.
* [leveldb](https://github.com/google/leveldb): Required by [/rpcz](rpcz.md) to record RPCs for tracing.

# Supported Environment

* [Ubuntu/LinuxMint/WSL](#ubuntulinuxmintwsl)
* [Fedora/CentOS](#fedoracentos)
* [Linux with self-built deps](#linux-with-self-built-deps)
* [MacOS](#macos)
* [Docker](#docker)

## Ubuntu/LinuxMint/WSL
### Prepare deps

Install common deps, [gflags](https://github.com/gflags/gflags), [protobuf](https://github.com/google/protobuf), [leveldb](https://github.com/google/leveldb):
```shell
sudo apt-get install -y git g++ make libssl-dev libgflags-dev libprotobuf-dev libprotoc-dev protobuf-compiler libleveldb-dev
```

If you need to statically link leveldb:
```shell
sudo apt-get install -y libsnappy-dev
```

If you need to enable cpu/heap profilers in examples:
```shell
sudo apt-get install -y libgoogle-perftools-dev
```

If you need to run tests, install and compile libgtest-dev (which is not compiled yet):
```shell
sudo apt-get install -y cmake libgtest-dev && cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv lib/libgtest* /usr/lib/ && cd -
```
The directory of gtest source code may be changed, try `/usr/src/googletest/googletest` if `/usr/src/gtest` is not there.

### Compile brpc with config_brpc.sh
git clone brpc, cd into the repo and run
```shell
$ sh config_brpc.sh --headers=/usr/include --libs=/usr/lib
$ make
```
To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

To use brpc with glog, add `--with-glog`.

To enable [thrift support](../en/thrift.md), install thrift first and add `--with-thrift`.

**Run example**

```shell
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```

Examples link brpc statically, if you need to link the shared version, `make clean` and `LINK_SO=1 make`

**Run tests**
```shell
$ cd test
$ make
$ sh run_tests.sh
```

### Compile brpc with cmake
```shell
mkdir build && cd build && cmake .. && cmake --build . -j6
```
With CMake 3.13+, we can also use the following commands to build the project:
```shell
cmake -B build && cmake --build build -j6
```
To help VSCode or Emacs(LSP) to understand code correctly, add `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` to generate `compile_commands.json`

To change compiler to clang, overwrite environment variable `CC` and `CXX` to `clang` and `clang++` respectively.

To not link debugging symbols, remove `build/CMakeCache.txt` and cmake with `-DWITH_DEBUG_SYMBOLS=OFF`

To use brpc with glog, cmake with `-DWITH_GLOG=ON`.

To enable [thrift support](../en/thrift.md), install thrift first and cmake with `-DWITH_THRIFT=ON`.

**Run example with cmake**

```shell
$ cd example/echo_c++
$ cmake -B build && cmake --build build -j4
$ ./echo_server &
$ ./echo_client
```
Examples link brpc statically, if you need to link the shared version, remove `CMakeCache.txt` and cmake with `-DLINK_SO=ON`

**Run tests**

```shell
$ mkdir build && cd build && cmake -DBUILD_UNIT_TESTS=ON .. && make && make test
```

### Compile brpc with vcpkg

[vcpkg](https://github.com/microsoft/vcpkg) is a package manager that supports all platforms,
you can use vcpkg to build brpc with the following step:

```shell
$ git clone https://github.com/microsoft/vcpkg.git
$ ./bootstrap-vcpkg.bat # for powershell
$ ./bootstrap-vcpkg.sh # for bash
$ ./vcpkg install brpc
```

## Fedora/CentOS

### Prepare deps

CentOS needs to install EPEL generally otherwise many packages are not available by default.
```shell
sudo yum install epel-release
```

Install common deps:
```shell
sudo yum install git gcc-c++ make openssl-devel
```

Install [gflags](https://github.com/gflags/gflags), [protobuf](https://github.com/google/protobuf), [leveldb](https://github.com/google/leveldb):
```shell
sudo yum install gflags-devel protobuf-devel protobuf-compiler leveldb-devel
```

If you need to enable cpu/heap profilers in examples:
```shell
sudo yum install gperftools-devel
```

If you need to run tests, install and compile gtest-devel (which is not compiled yet):
```shell
sudo yum install gtest-devel
```

### Compile brpc with config_brpc.sh

git clone brpc, cd into the repo and run

```shell
$ sh config_brpc.sh --headers="/usr/include" --libs="/usr/lib64 /usr/bin"
$ make
```
To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

To use brpc with glog, add `--with-glog`.

To enable [thrift support](../en/thrift.md), install thrift first and add `--with-thrift`.

**Run example**

```shell
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```

Examples link brpc statically, if you need to link the shared version, `make clean` and `LINK_SO=1 make`

**Run tests**
```shell
$ cd test
$ make
$ sh run_tests.sh
```

### Compile brpc with cmake
Same with [here](#compile-brpc-with-cmake)

## Linux with self-built deps

### Prepare deps

brpc builds itself to both static and shared libs by default, so it needs static and shared libs of deps to be built as well.

Take [gflags](https://github.com/gflags/gflags) as example, which does not build shared lib by default, you need to pass options to `cmake` to change the behavior:
```shell
$ cmake . -DBUILD_SHARED_LIBS=1 -DBUILD_STATIC_LIBS=1
$ make
```

### Compile brpc

Keep on with the gflags example, let `../gflags_dev` be where gflags is cloned.

git clone brpc. cd into the repo and run

```shell
$ sh config_brpc.sh --headers="../gflags_dev /usr/include" --libs="../gflags_dev /usr/lib64"
$ make
```

Here we pass multiple paths to `--headers` and `--libs` to make the script search for multiple places. You can also group all deps and brpc into one directory, then pass the directory to --headers/--libs which actually search all subdirectories recursively and will find necessary files.

To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

To use brpc with glog, add `--with-glog`.

To enable [thrift support](../en/thrift.md), install thrift first and add `--with-thrift`.

```shell
$ ls my_dev
gflags_dev protobuf_dev leveldb_dev brpc_dev
$ cd brpc_dev
$ sh config_brpc.sh --headers=.. --libs=..
$ make
```

### Compile brpc with cmake
Same with [here](#compile-brpc-with-cmake)

## MacOS

Note: With same environment, the performance of the MacOS version is worse than the Linux version. If your service is performance-critical, do not use MacOS as your production environment.

### Apple Silicon

The code at master HEAD already supports M1 series chips. M2 series are not tested yet. Please feel free to report remaining warnings/errors to us by issues.

## Docker
Compile brpc with docker:

```shell
$ mkdir -p ~/brpc
$ cd ~/brpc
$ git clone https://github.com/apache/brpc.git
$ cd brpc
$ docker build -t brpc:master .
$ docker images
$ docker run -it brpc:master /bin/bash
```

### Prepare deps

Install dependencies:
```shell
brew install openssl git gnu-getopt coreutils gflags protobuf leveldb
```

If you need to enable cpu/heap profilers in examples:
```shell
brew install gperftools
```

If you need to run tests, googletest is required. Run `brew install googletest` first to see if it works. If not (old homebrew does not have googletest), you can download and compile googletest by your own:
```shell
git clone https://github.com/google/googletest -b release-1.10.0 && cd googletest/googletest && mkdir build && cd build && cmake -DCMAKE_CXX_FLAGS="-std=c++11" .. && make
```
After the compilation, copy `include/` and `lib/` into `/usr/local/include` and `/usr/local/lib` respectively to expose gtest to all apps

### OpenSSL

openssl installed in Monterey may not be found at `/usr/local/opt/openssl`, instead it's probably put under `/opt/homebrew/Cellar`. If the compiler cannot find openssl：

* Run `brew link openssl --force` first and check if `/usr/local/opt/openssl` appears.
* If above command does not work, consider making a soft link using `sudo ln -s /opt/homebrew/Cellar/openssl@3/3.0.3 /usr/local/opt/openssl`. Note that the installed openssl in above command may be put in different places in different environments, which could be revealed by running `brew info openssl`.

### Compile brpc with config_brpc.sh
git clone brpc, cd into the repo and run
```shell
$ sh config_brpc.sh --headers=/usr/local/include --libs=/usr/local/lib --cc=clang --cxx=clang++
$ make
```

The homebrew in Monterey may install software at different directories from before. If path related errors are reported, try setting headers/libs like below:

```shell
$ sh config_brpc.sh --headers=/opt/homebrew/include --libs=/opt/homebrew/lib --cc=clang --cxx=clang++
$ make
```

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

To use brpc with glog, add `--with-glog`.

To enable [thrift support](../en/thrift.md), install thrift first and add `--with-thrift`.

**Run example**

```shell
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```

Examples link brpc statically, if you need to link the shared version, `make clean` and `LINK_SO=1 make`

**Run tests**
```shell
$ cd test
$ make
$ sh run_tests.sh
```

### Compile brpc with cmake
Same with [here](#compile-brpc-with-cmake)

# Supported deps

## GCC: 4.8-11.2

c++11 is turned on by default to remove dependencies on boost (atomic).

The over-aligned issues in GCC7 is suppressed temporarily now.

Using other versions of gcc may generate warnings, contact us to fix.

Adding `-D__const__=__unused__` to cxxflags in your makefiles is a must to avoid [errno issue in gcc4+](thread_local.md).

## Clang: 3.5-4.0

no known issues.

## glibc: 2.12-2.25

no known issues.

## protobuf: 2.4+

Be compatible with pb 3.x and pb 2.x with the same file:
Don't use new types in proto3 and start the proto file with `syntax="proto2";`
[tools/add_syntax_equal_proto2_to_all.sh](https://github.com/apache/brpc/blob/master/tools/add_syntax_equal_proto2_to_all.sh)can add `syntax="proto2"` to all proto files without it.

Arena in pb 3.x is not supported yet.

## gflags: 2.0-2.2.1

no known issues.

## openssl: 0.97-1.1

required by https.

## tcmalloc: 1.7-2.5

brpc does **not** link [tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html) by default. Users link tcmalloc on-demand.

Comparing to ptmalloc embedded in glibc, tcmalloc often improves performance. However different versions of tcmalloc may behave really differently. For example, tcmalloc 2.1 may make multi-threaded examples in brpc perform significantly worse(due to a spinlock in tcmalloc) than the one using tcmalloc 1.7 and 2.5. Even different minor versions may differ. When you program behave unexpectedly, remove tcmalloc or try another version.

Code compiled with gcc 4.8.2 and linked to a tcmalloc compiled with earlier GCC may crash or deadlock before main(), E.g:

![img](../images/tcmalloc_stuck.png)

When you meet the issue, compile tcmalloc with the same GCC.

Another common issue with tcmalloc is that it does not return memory to system as early as ptmalloc. So when there's an invalid memory access, the program may not crash directly, instead it crashes at a unrelated place, or even not crash. When you program has weird memory issues, try removing tcmalloc.

If you want to use [cpu profiler](cpu_profiler.md) or [heap profiler](heap_profiler.md), do link `libtcmalloc_and_profiler.a`. These two profilers are based on tcmalloc.[contention profiler](contention_profiler.md) does not require tcmalloc.

When you remove tcmalloc, not only remove the linkage with tcmalloc but also the macro `-DBRPC_ENABLE_CPU_PROFILER`.

## glog: 3.3+

brpc implements a default [logging utility](../../src/butil/logging.h) which conflicts with glog. To replace this with glog, add *--with-glog* to config_brpc.sh or add `-DWITH_GLOG=ON` to cmake.

## valgrind: 3.8+

brpc detects valgrind automatically (and registers stacks of bthread). Older valgrind(say 3.2) is not supported.

## thrift: 0.9.3-0.11.0

no known issues.

# Track instances

We provide a program to help you to track and monitor all brpc instances. Just run [trackme_server](https://github.com/apache/brpc/tree/master/tools/trackme_server/) somewhere and launch need-to-be-tracked instances with -trackme_server=SERVER. The trackme_server will receive pings from instances periodically and print logs when it does. You can aggregate instance addresses from the log and call builtin services of the instances for further information.
