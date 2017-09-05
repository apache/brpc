The opensource version of baidu-rpc

# BUILD

baidu-rpc prefers static linking if possible, so that deps don't have to be installed on every
machine running the code. 

## Ubuntu/LinuxMint/WSL
### compile
1. install common deps: git g++ make libssl-dev
2. install gflags protobuf leveldb: libgflags-dev libprotobuf-dev libprotoc-dev protobuf-compiler libleveldb-dev. If you need to statically link leveldb, install libsnappy-dev as well.
3. git clone this repo. cd into the repo and run
```
$ sh config_brpc.sh --headers=/usr/include --libs=/usr/lib
```
4. make

### run example
```
$ cd example/echo_c++
$ make
$ ./echo_server &
$ ./echo_client
```

### run examples with cpu/heap profilers
Install libgoogle-perftools-dev and re-run config_brpc.sh before compiling

### compile tests
Install gmock and gtest, use the gtest embedded in gmock and don't install libgtest-dev
```
$ sudo apt-get install google-mock
$ cd /usr/src
$ sudo cmake .
$ sudo make
$ sudo mv lib*.a gtest/lib*.a /usr/lib
$ sudo mv gtest/include/gtest /usr/include/
```
Rerun config_brpc.sh and run make in test/

# Supported deps

## GCC: 4.8-7.1

c++11 is  turned on by default to remove dependency on boost (atomic).

The over-aligned issues in GCC7 is suppressed temporarily now.

Using other versions of gcc may generate warnings, contact us to fix.

Adding `-D__const__=` to cxxflags in your makefiles is a must avoid [errno issue in gcc4+](docs/thread_local.md)。

## Clang: 3.5-4.0

unittests can't be compiled with clang yet.

## glibc: 2.12-2.25

no known issues.

## protobuf: 2.4-3.2

Be compatible with pb 3.0 and pb 2.x with the same file: 
Don't use new types in proto3 and start the proto file with `syntax="proto2";`
[tools/add_syntax_equal_proto2_to_all.sh](http://icode.baidu.com/repo/baidu/opensource/baidu-rpc/files/master/blob/tools/add_syntax_equal_proto2_to_all.sh)can add `syntax="proto2"` to all proto files without it.
protobuf 3.3-3.4 is not tested yet.

## gflags: 2.0-2.21

no known issues.

## openssl: 0.97-1.1

required by https.

## tcmalloc: 1.7-2.5

baidu-rpc does **not** link [tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html) by default. Users link tcmalloc on-demand.

Comparing to ptmalloc embedded in glibc, tcmalloc often improves performance. However different versions of tcmalloc may behave really differently. For example, tcmalloc 2.1 may make multi-threaded examples in baidu-rpc perform significantly worse(due to a spinlock in tcmalloc) than the one using tcmalloc 1.7 and 2.5. Even different minor versions may differ. When you program behave unexpectedly, remove tcmalloc or try another version.

Code compiled with gcc 4.8.2 when linking to a tcmalloc compiled with earlier GCC may crash or deadlock before main(), E.g:

![img](http://wiki.baidu.com/download/attachments/71337200/image2017-8-22%2017%3A32%3A28.png?version=1&modificationDate=1503394348000&api=v2)

When you meet the issue, compile tcmalloc with the same GCC as the RPC code.

Another common issue with tcmalloc is that it does not return memory to system as early as ptmalloc. So when there's an invalid memory access, the program may not crash directly,  instead it crashes at a unrelated place, or even not crash. When you program has weird memory issues, try removing tcmalloc.

If you want to use [cpu profiler](docs/cpu_profiler.md) or [heap profiler](docs/heap_profiler.md), do link `libtcmalloc_and_profiler.a`. These two profilers are based on tcmalloc.[contention profiler](contention_profiler.md) does not require tcmalloc.

When you remove tcmalloc, not only remove the linking with tcmalloc but also the macros: `-DBRPC_ENABLE_CPU_PROFILER` and `-DBRPC_ENABLE_HEAP_PROFILER`.

## valgrind: 3.8+

baidu-rpc detects valgrind automatically (and registers stacks of bthread). Older valgrind (say 3.2) is not supported.

# Track instances

We provide a program to help you to track and monitor all baidu-rpc instances. Just run [trackme_server](tools/trackme_server/trackme_server.cpp) somewhere and launch need-to-be-tracked instances with -trackme_server=<SERVER>. The trackme_server will receive pings from instance periodically and print logs when it does. You can aggregate instance addresses from the log and call builtin services of the instances for further information.