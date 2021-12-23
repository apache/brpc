brpc can analyze which functions occupy the memory. The principle of the heap profiler is to sample the stack at the call site every time some memory is allocated. "Some" is controlled by the environment variable TCMALLOC_SAMPLE_PARAMETER. The default is 524288, which is 512K bytes. The function call relationship shown by the stack is summarized as the result graph we see. In practice, the impact of the heap profiler on the original program is not obvious.

# How to open

1. Link to `libtcmalloc_and_profiler.a`

   1. If tcmalloc uses frame pointer instead of libunwind backtracking stack, please make sure to add `-fno-omit-frame-pointer` to CXXFLAGS or CFLAGS, otherwise the calling relationship between functions will be lost, and the resulting pictures will all be each other Independent function box.

2. In the shell `export TCMALLOC_SAMPLE_PARAMETER=524288`. This variable means to make a statistics every time so many bytes of memory are allocated. The default value is 0, which means that the memory statistics are not turned on. [Official document](http://goog-perftools.sourceforge.net/doc/tcmalloc.html) It is recommended to set it to 524288. This variable can also be set temporarily before running, such as `TCMALLOC_SAMPLE_PARAMETER=524288 ./server`. If there is no such environment variable, you may see this result:

   ```
   $ tools/pprof --text localhost:9002/pprof/heap           
   Fetching /pprof/heap profile from http://localhost:9002/pprof/heap to
     /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap
   Wrote profile to /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap
   /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap: header size >= 2**16
   ```

3. If you only use brpc client or do not use brpc, see [here](dummy_server.md).

Be careful to turn off the authentication on the server side, otherwise you may see this:

```
$ tools/pprof --text localhost:9002/pprof/heap
Use of uninitialized value in substitution (s///) at tools/pprof line 2703.
http://localhost:9002/pprof/symbol doesn't exist
```

There may be logs like this on the server side:

```
FATAL: 12-26 10:01:25: * 0 [src/brpc/policy/giano_authenticator.cpp:65][4294969345] Giano fails to verify credentical, 70003
WARNING: 12-26 10:01:25: * 0 [src/brpc/input_messenger.cpp:132][4294969345] Authentication failed, remote side(127.0.0.1:22989) of sockfd=5, close it
```

# Icon

![img](../images/heap_profiler_1.png)

The upper left corner is the total amount of memory allocated by malloc by the current program. Follow the number on the arrow to see which functions the memory comes from.

Click the text selection box in the upper left corner to view the results in text format. Sometimes this sorting by allocation is more convenient.

![img](../images/heap_profiler_2.png)

The functions of the two selection boxes in the upper left corner are:

-View: The profile currently being viewed. Select \<new profile\> to create a new profile. After the creation is complete, a new profile will appear in the View selection box, and the URL will be modified to the corresponding address. This means that you can share the results by pasting the URL, and the person who clicks on the link will see exactly the same results as you, instead of redoing the profiling results. You can select the previous profile view in the box. The history profile keeps the most recent 32, which can be adjusted through [--max_profiles_kept](http://brpc.baidu.com:8765/flags/max_profiles_kept).
-Diff: Compare with the selected profile. <none> means to choose nothing. If you select a previous profile, you will see the amount of change in the profile in the View box compared to the profile in the Diff box.

The following figure demonstrates the effect of checking Diff and Text.

![img](../images/heap_profiler_3.gif)

Under Linux, you can also use the pprof script (tools/pprof) to view the results in text format on the command line:

```
$ tools/pprof --text db-rpc-dev00.db01:8765/pprof/heap    
Fetching /pprof/heap profile from http://db-rpc-dev00.db01:8765/pprof/heap to
  /home/gejun/pprof/play_server.1453216025.db-rpc-dev00.db01.pprof.heap
Wrote profile to /home/gejun/pprof/play_server.1453216025.db-rpc-dev00.db01.pprof.heap
Adjusting heap profiles for 1-in-524288 sampling rate
Heap version 2
Total: 38.9 MB
    35.8 92.0% 92.0% 35.8 92.0% ::cpp_alloc
     2.1 5.4% 97.4% 2.1 5.4% butil::FlatMap
     0.5 1.3% 98.7% 0.5 1.3% butil::IOBuf::append
     0.5 1.3% 100.0% 0.5 1.3% butil::IOBufAsZeroCopyOutputStream::Next
     0.0 0.0% 100.0% 0.6 1.5% MallocExtension::GetHeapSample
     0.0 0.0% 100.0% 0.5 1.3% ProfileHandler::Init
     0.0 0.0% 100.0% 0.5 1.3% ProfileHandlerRegisterCallback
     0.0 0.0% 100.0% 0.5 1.3% __do_global_ctors_aux
     0.0 0.0% 100.0% 1.6 4.2% _end
     0.0 0.0% 100.0% 0.5 1.3% _init
     0.0 0.0% 100.0% 0.6 1.5% brpc::CloseIdleConnections
     0.0 0.0% 100.0% 1.1 2.9% brpc::GlobalUpdate
     0.0 0.0% 100.0% 0.6 1.5% brpc::PProfService::heap
     0.0 0.0% 100.0% 1.9 4.9% brpc::Socket::Create
     0.0 0.0% 100.0% 2.9 7.4% brpc::Socket::Write
     0.0 0.0% 100.0% 3.8 9.7% brpc::Span::CreateServerSpan
     0.0 0.0% 100.0% 1.4 3.5% brpc::SpanQueue::Push
     0.0 0.0% 100.0% 1.9 4.8% butil::ObjectPool
     0.0 0.0% 100.0% 0.8 2.0% butil::ResourcePool
     0.0 0.0% 100.0% 1.0 2.6% butil::iobuf::tls_block
     0.0 0.0% 100.0% 1.0 2.6% bthread::TimerThread::Bucket::schedule
     0.0 0.0% 100.0% 1.6 4.1% bthread::get_stack
     0.0 0.0% 100.0% 4.2 10.8% bthread_id_create
     0.0 0.0% 100.0% 1.1 2.9% bvar::Variable::describe_series_exposed
     0.0 0.0% 100.0% 1.0 2.6% bvar::detail::AgentGroup
     0.0 0.0% 100.0% 0.5 1.3% bvar::detail::Percentile::operator
     0.0 0.0% 100.0% 0.5 1.3% bvar::detail::PercentileSamples
     0.0 0.0% 100.0% 0.5 1.3% bvar::detail::Sampler::schedule
     0.0 0.0% 100.0% 6.5 16.8% leveldb::Arena::AllocateNewBlock
     0.0 0.0% 100.0% 0.5 1.3% leveldb::VersionSet::LogAndApply
     0.0 0.0% 100.0% 4.2 10.8% pthread_mutex_unlock
     0.0 0.0% 100.0% 0.5 1.3% pthread_once
     0.0 0.0% 100.0% 0.5 1.3% std::_Rb_tree
     0.0 0.0% 100.0% 1.5 3.9% std::basic_string
     0.0 0.0% 100.0% 3.5 9.0% std::string::_Rep::_S_create
```

brpc also provides a similar growth profiler to analyze the whereabouts of memory allocation (without considering release).

![img](../images/growth_profiler.png)

# MacOS additional configuration

1. Install [standalone pprof](https://github.com/google/pprof), and write the path of the downloaded pprof binary file into the environment variable GOOGLE_PPROF_BINARY_PATH
2. Install llvm-symbolizer (convert function symbols to function names) and install directly with brew: `brew install llvm`