# BUILD

brpc prefers static linking if possible, so that deps don't have to be installed on every machine running the code.

brpc depends on following packages:

* [gflags](https://github.com/gflags/gflags): Extensively used to specify global options.
* [protobuf](https://github.com/google/protobuf): needless to say, pb is a must-have dep.
* [leveldb](https://github.com/google/leveldb): required by [/rpcz](rpcz.md) to record RPCs for tracing.

## Ubuntu/LinuxMint/WSL
### Prepare deps

Install common deps:
```
$ sudo apt-get install git g++ make libssl-dev
```

Install [gflags](https://github.com/gflags/gflags), [protobuf](https://github.com/google/protobuf), [leveldb](https://github.com/google/leveldb):
```
$ sudo apt-get install libgflags-dev libprotobuf-dev libprotoc-dev protobuf-compiler libleveldb-dev
```

If you need to statically link leveldb:
```
$ sudo apt-get install libsnappy-dev
```

### Compile brpc
git clone brpc, cd into the repo and run
```
$ sh config_brpc.sh --headers=/usr/include --libs=/usr/lib
$ make
```
To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

### Run example

```
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```
Examples link brpc statically, if you need to link the shared version, `make clean` and `LINK_SO=1 make`

To run examples with cpu/heap profilers, install `libgoogle-perftools-dev` and re-run `config_brpc.sh` before compiling.

### Run tests
Install and compile libgtest-dev (which is not compiled yet):

```shell
sudo apt-get install libgtest-dev
cd /usr/src/gtest && sudo cmake . && sudo make && sudo mv libgtest* /usr/lib/
```

The directory of gtest source code may be changed, try `/usr/src/googletest/googletest` if `/usr/src/gtest` is not there.

Rerun `config_brpc.sh`, `make` in test/, and `sh run_tests.sh`

## Fedora/CentOS

### Prepare deps

CentOS needs to install EPEL generally otherwise many packages are not available by default.
```
sudo yum install epel-release
```

Install common deps:
```
sudo yum install git g++ make openssl-devel
```

Install [gflags](https://github.com/gflags/gflags), [protobuf](https://github.com/google/protobuf), [leveldb](https://github.com/google/leveldb):
```
sudo yum install gflags-devel protobuf-devel protobuf-compiler leveldb-devel
```
### Compile brpc

git clone brpc, cd into the repo and run

```
$ sh config_brpc.sh --headers=/usr/include --libs=/usr/lib64
$ make
```
To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

### Run example

```
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```
Examples link brpc statically, if you need to link the shared version, `make clean` and `LINK_SO=1 make`

To run examples with cpu/heap profilers, install `gperftools-devel` and re-run `config_brpc.sh` before compiling.

### Run tests

Install gtest-devel.

Rerun `config_brpc.sh`, `make` in test/, and `sh run_tests.sh`

## Linux with self-built deps

### Prepare deps

brpc builds itself to both static and shared libs by default, so it needs static and shared libs of deps to be built as well.

Take [gflags](https://github.com/gflags/gflags) as example, which does not build shared lib by default, you need to pass options to `cmake` to change the behavior:
```
cmake . -DBUILD_SHARED_LIBS=1 -DBUILD_STATIC_LIBS=1
make
```

### Compile brpc

Keep on with the gflags example, let `../gflags_dev` be where gflags is cloned.

git clone brpc. cd into the repo and run

```
$ sh config_brpc.sh --headers="../gflags_dev /usr/include" --libs="../gflags_dev /usr/lib64"
$ make
```

To change compiler to clang, add `--cxx=clang++ --cc=clang`.

To not link debugging symbols, add `--nodebugsymbols` and compiled binaries will be much smaller.

Here we pass multiple paths to `--headers` and `--libs` to make the script search for multiple places. You can also group all deps and brpc into one directory, then pass the directory to --headers/--libs which actually search all subdirectories recursively and will find necessary files.

```
$ ls my_dev
gflags_dev protobuf_dev leveldb_dev brpc_dev
$ cd brpc_dev
$ sh config_brpc.sh --headers=.. --libs=..
$ make
```

# Supported deps

## GCC: 4.8-7.1

c++11 is  turned on by default to remove dependency on boost (atomic).

The over-aligned issues in GCC7 is suppressed temporarily now.

Using other versions of gcc may generate warnings, contact us to fix.

Adding `-D__const__=` to cxxflags in your makefiles is a must avoid [errno issue in gcc4+](thread_local.md)。

## Clang: 3.5-4.0

unittests can't be compiled with clang yet.

## glibc: 2.12-2.25

no known issues.

## protobuf: 2.4-3.2

Be compatible with pb 3.0 and pb 2.x with the same file:
Don't use new types in proto3 and start the proto file with `syntax="proto2";`
[tools/add_syntax_equal_proto2_to_all.sh](https://github.com/brpc/brpc/blob/master/tools/add_syntax_equal_proto2_to_all.sh)can add `syntax="proto2"` to all proto files without it.
protobuf 3.3-3.4 is not tested yet.

## gflags: 2.0-2.21

no known issues.

## openssl: 0.97-1.1

required by https.

## tcmalloc: 1.7-2.5

brpc does **not** link [tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html) by default. Users link tcmalloc on-demand.

Comparing to ptmalloc embedded in glibc, tcmalloc often improves performance. However different versions of tcmalloc may behave really differently. For example, tcmalloc 2.1 may make multi-threaded examples in brpc perform significantly worse(due to a spinlock in tcmalloc) than the one using tcmalloc 1.7 and 2.5. Even different minor versions may differ. When you program behave unexpectedly, remove tcmalloc or try another version.

Code compiled with gcc 4.8.2 when linking to a tcmalloc compiled with earlier GCC may crash or deadlock before main(), E.g:

![img](../images/tcmalloc_stuck.png)

When you meet the issue, compile tcmalloc with the same GCC as the RPC code.

Another common issue with tcmalloc is that it does not return memory to system as early as ptmalloc. So when there's an invalid memory access, the program may not crash directly,  instead it crashes at a unrelated place, or even not crash. When you program has weird memory issues, try removing tcmalloc.

If you want to use [cpu profiler](cpu_profiler.md) or [heap profiler](heap_profiler.md), do link `libtcmalloc_and_profiler.a`. These two profilers are based on tcmalloc.[contention profiler](contention_profiler.md) does not require tcmalloc.

When you remove tcmalloc, not only remove the linking with tcmalloc but also the macros: `-DBRPC_ENABLE_CPU_PROFILER`.

## glog: 3.3+

brpc implementes a default [logging utility](../../src/butil/logging.h) which conflicts with glog. To replace this with glog, add *--with-glog* to config_brpc.sh

## valgrind: 3.8+

brpc detects valgrind automatically (and registers stacks of bthread). Older valgrind (say 3.2) is not supported.

# Track instances

We provide a program to help you to track and monitor all brpc instances. Just run [trackme_server](https://github.com/brpc/brpc/tree/master/tools/trackme_server/) somewhere and launch need-to-be-tracked instances with -trackme_server=SERVER. The trackme_server will receive pings from instances periodically and print logs when it does. You can aggregate instance addresses from the log and call builtin services of the instances for further information.
