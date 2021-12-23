brpc can analyze hot functions in the program.

# How to open

1. Link to `libtcmalloc_and_profiler.a`
   1. This writing also enables tcmalloc. It is not recommended to link the cpu profiler alone without linking tcmalloc. It may cause [crash](https://github.com/gperftools/gperftools/blob/master/README#L226). Since tcmalloc does not return the memory in time, cross-boundary access will not crash.
   2. If tcmalloc uses frame pointer instead of libunwind backtracking stack, please make sure to add `-fno-omit-frame-pointer` in CXXFLAGS or CFLAGS, otherwise the calling relationship between functions will be lost, and the resulting pictures will all be each other Independent function box.
2. Define the macro BRPC_ENABLE_CPU_PROFILER, and generally add the compilation parameter -DBRPC_ENABLE_CPU_PROFILER.
3. If you only use brpc client or do not use brpc, see [here](dummy_server.md).

Be careful to turn off the authentication on the server side, otherwise you may see this:

```
$ tools/pprof --text localhost:9002/pprof/profile
Use of uninitialized value in substitution (s///) at tools/pprof line 2703.
http://localhost:9002/profile/symbol doesn't exist
```

There may be logs like this on the server side:

```
FATAL: 12-26 10:01:25: * 0 [src/brpc/policy/giano_authenticator.cpp:65][4294969345] Giano fails to verify credentical, 70003
WARNING: 12-26 10:01:25: * 0 [src/brpc/input_messenger.cpp:132][4294969345] Authentication failed, remote side(127.0.0.1:22989) of sockfd=5, close it
```

# View method

1. View through the /hotspots/cpu page of the builtin service
1. View through the pprof tool, such as tools/pprof --text localhost:9002/pprof/profile

# Control sampling frequency

Set environment variables before starting: export CPUPROFILE_FREQUENCY=xxx

Default value: 100

# Control sampling time

url plus?seconds=seconds, such as /hotspots/cpu?seconds=5

# Icon

The following figure is the result of running the cpu profiler once:

-The upper left corner is the overall information, including time, program name, total number of samples and so on.
-In the View box, you can choose to view the profile results that have been run before, and in the Diff box you can choose to view the amount of change from the previous results, and it will be cleared after restarting.
-The fields in the box representing the function call from top to bottom are: function name, the number and proportion of samples taken by this function itself (excluding all sub-functions), the cumulative number and proportion of samples of this function and all sub-functions called . The larger the number of samples, the larger the frame.
-The number on the line between the boxes indicates the number of calls of the upper-level function that is sampled to the lower-level function. The larger the number, the thicker the line.

Hot spot analysis generally starts with finding the largest frame and the thickest line to investigate its source and whereabouts.

The principle of the cpu profiler is to sample the stack of the thread in the SIGPROF handler that is called regularly. Since the handler (after Linux 2.6) will be randomly placed on the stack of the active thread to run, the cpu profiler can run after a period of time. With a high probability, the active functions in all active threads are collected, and finally summarized into a call graph according to the function call relationship represented by the stack, and the address is converted into a symbol. This is the result graph we see. The acquisition frequency is controlled by the environmental variable CPUPROFILE_FREQUENCY, the default is 100, which means 100 times per second or once every 10ms. In practice, the influence of the cpu profiler on the original program is not obvious.

![img](../images/echo_cpu_profiling.png)

Under Linux, you can also use [pprof](https://github.com/brpc/brpc/blob/master/tools/pprof) or pprof in gperftools for profiling.

For example, `pprof --text localhost:9002 --seconds=5` means to count the CPU status of the server running on port 9002 of this machine, and the duration is 5 seconds. An example of a run is as follows:

```
$ tools/pprof --text 0.0.0.0:9002 --seconds=5
Gathering CPU profile from http://0.0.0.0:9002/pprof/profile?seconds=5 for 5 seconds to
  /home/gejun/pprof/echo_server.1419501210.0.0.0.0
Be patient...
Wrote profile to /home/gejun/pprof/echo_server.1419501210.0.0.0.0
Removing funlockfile from all stack traces.
Total: 2946 samples
    1161 39.4% 39.4% 1161 39.4% syscall
     248 8.4% 47.8% 248 8.4% bthread::TaskControl::steal_task
     227 7.7% 55.5% 227 7.7% writev
      87 3.0% 58.5% 88 3.0% ::cpp_alloc
      74 2.5% 61.0% 74 2.5% __read_nocancel
      46 1.6% 62.6% 48 1.6% tc_delete
      42 1.4% 64.0% 42 1.4% brpc::Socket::Address
      41 1.4% 65.4% 41 1.4% epoll_wait
      35 1.2% 66.6% 35 1.2% memcpy
      33 1.1% 67.7% 33 1.1% __pthread_getspecific
      33 1.1% 68.8% 33 1.1% brpc::Socket::Write
      33 1.1% 69.9% 33 1.1% epoll_ctl
      28 1.0% 70.9% 42 1.4% brpc::policy::ProcessRpcRequest
      27 0.9% 71.8% 27 0.9% butil::IOBuf::_push_back_ref
      27 0.9% 72.7% 27 0.9% bthread::TaskGroup::ending_sched
```

Omit –text to enter the interactive mode, as shown in the following figure:

```
$ tools/pprof localhost:9002 --seconds=5       
Gathering CPU profile from http://0.0.0.0:9002/pprof/profile?seconds=5 for 5 seconds to
  /home/gejun/pprof/echo_server.1419501236.0.0.0.0
Be patient...
Wrote profile to /home/gejun/pprof/echo_server.1419501236.0.0.0.0
Removing funlockfile from all stack traces.
Welcome to pprof! For help, type'help'.
(pprof) top
Total: 2954 samples
    1099 37.2% 37.2% 1099 37.2% syscall
     253 8.6% 45.8% 253 8.6% bthread::TaskControl::steal_task
     240 8.1% 53.9% 240 8.1% writev
      90 3.0% 56.9% 90 3.0% ::cpp_alloc
      67 2.3% 59.2% 67 2.3% __read_nocancel
      47 1.6% 60.8% 47 1.6% butil::IOBuf::_push_back_ref
      42 1.4% 62.2% 56 1.9% brpc::policy::ProcessRpcRequest
      41 1.4% 63.6% 41 1.4% epoll_wait
      38 1.3% 64.9% 38 1.3% epoll_ctl
      37 1.3% 66.1% 37 1.3% memcpy
      35 1.2% 67.3% 35 1.2% brpc::Socket::Address
```

# MacOS additional configuration

Under MacOS, the perl pprof script in gperftools cannot convert function addresses into function names. The solution is:

1. Install [standalone pprof](https://github.com/google/pprof), and write the path of the downloaded pprof binary file into the environment variable GOOGLE_PPROF_BINARY_PATH
2. Install llvm-symbolizer (convert function symbols to function names) and install directly with brew: `brew install llvm`

# Flame graph

If you need the results to be displayed in flame graphs, please download and install the [FlameGraph](https://github.com/brendangregg/FlameGraph) tool, and correctly set the environment variable FLAMEGRAPH_PL_PATH to the local /path/to/flamegraph.pl Just start the server.