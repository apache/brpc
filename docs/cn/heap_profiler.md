brpc可以分析内存是被哪些函数占据的。heap profiler的原理是每分配满一些内存就采样调用处的栈，“一些”由环境变量TCMALLOC_SAMPLE_PARAMETER控制，默认524288，即512K字节。根据栈表现出的函数调用关系汇总为我们看到的结果图。在实践中heap profiler对原程序的影响不明显。

# 开启方法

1. 链接`libtcmalloc_and_profiler.a`

   1. 如果tcmalloc使用frame pointer而不是libunwind回溯栈，请确保在CXXFLAGS或CFLAGS中加上`-fno-omit-frame-pointer`，否则函数间的调用关系会丢失，最后产生的图片中都是彼此独立的函数方框。

2. 在shell中`export TCMALLOC_SAMPLE_PARAMETER=524288`。该变量指每分配这么多字节内存时做一次统计，默认为0，代表不开启内存统计。[官方文档](http://goog-perftools.sourceforge.net/doc/tcmalloc.html)建议设置为524288。这个变量也可在运行前临时设置，如`TCMALLOC_SAMPLE_PARAMETER=524288 ./server`。如果没有这个环境变量，可能会看到这样的结果：

   ```
   $ tools/pprof --text localhost:9002/pprof/heap           
   Fetching /pprof/heap profile from http://localhost:9002/pprof/heap to
     /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap
   Wrote profile to /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap
   /home/gejun/pprof/echo_server.1419559063.localhost.pprof.heap: header size >= 2**16
   ```

3. 如果只是brpc client或没有使用brpc，看[这里](dummy_server.md)。 

注意要关闭Server端的认证，否则可能会看到这个：

```
$ tools/pprof --text localhost:9002/pprof/heap
Use of uninitialized value in substitution (s///) at tools/pprof line 2703.
http://localhost:9002/pprof/symbol doesn't exist
```

server端可能会有这样的日志：

```
FATAL: 12-26 10:01:25:   * 0 [src/brpc/policy/giano_authenticator.cpp:65][4294969345] Giano fails to verify credentical, 70003
WARNING: 12-26 10:01:25:   * 0 [src/brpc/input_messenger.cpp:132][4294969345] Authentication failed, remote side(127.0.0.1:22989) of sockfd=5, close it
```

# 图示

![img](../images/heap_profiler_1.png)

左上角是当前程序通过malloc分配的内存总量，顺着箭头上的数字可以看到内存来自哪些函数。方框内第一个百分比是本函数的内存占用比例, 第二个百分比是本函数及其子函数的内存占用比例

点击左上角的text选择框可以查看文本格式的结果，有时候这种按分配量排序的形式更方便。

![img](../images/heap_profiler_2.png)

左上角的两个选择框作用分别是：

- View：当前正在看的profile。选择\<new profile\>表示新建一个。新建完毕后，View选择框中会出现新profile，URL也会被修改为对应的地址。这意味着你可以通过粘贴URL分享结果，点击链接的人将看到和你一模一样的结果，而不是重做profiling的结果。你可以在框中选择之前的profile查看。历史profiie保留最近的32个，可通过[--max_profiles_kept](http://brpc.baidu.com:8765/flags/max_profiles_kept)调整。
- Diff：和选择的profile做对比。<none>表示什么都不选。如果你选择了之前的某个profile，那么将看到View框中的profile相比Diff框中profile的变化量。

下图演示了勾选Diff和Text的效果。

![img](../images/heap_profiler_3.gif)

在Linux下，你也可以使用pprof脚本（tools/pprof）在命令行中查看文本格式结果：

```
$ tools/pprof --text db-rpc-dev00.db01:8765/pprof/heap    
Fetching /pprof/heap profile from http://db-rpc-dev00.db01:8765/pprof/heap to
  /home/gejun/pprof/play_server.1453216025.db-rpc-dev00.db01.pprof.heap
Wrote profile to /home/gejun/pprof/play_server.1453216025.db-rpc-dev00.db01.pprof.heap
Adjusting heap profiles for 1-in-524288 sampling rate
Heap version 2
Total: 38.9 MB
    35.8  92.0%  92.0%     35.8  92.0% ::cpp_alloc
     2.1   5.4%  97.4%      2.1   5.4% butil::FlatMap
     0.5   1.3%  98.7%      0.5   1.3% butil::IOBuf::append
     0.5   1.3% 100.0%      0.5   1.3% butil::IOBufAsZeroCopyOutputStream::Next
     0.0   0.0% 100.0%      0.6   1.5% MallocExtension::GetHeapSample
     0.0   0.0% 100.0%      0.5   1.3% ProfileHandler::Init
     0.0   0.0% 100.0%      0.5   1.3% ProfileHandlerRegisterCallback
     0.0   0.0% 100.0%      0.5   1.3% __do_global_ctors_aux
     0.0   0.0% 100.0%      1.6   4.2% _end
     0.0   0.0% 100.0%      0.5   1.3% _init
     0.0   0.0% 100.0%      0.6   1.5% brpc::CloseIdleConnections
     0.0   0.0% 100.0%      1.1   2.9% brpc::GlobalUpdate
     0.0   0.0% 100.0%      0.6   1.5% brpc::PProfService::heap
     0.0   0.0% 100.0%      1.9   4.9% brpc::Socket::Create
     0.0   0.0% 100.0%      2.9   7.4% brpc::Socket::Write
     0.0   0.0% 100.0%      3.8   9.7% brpc::Span::CreateServerSpan
     0.0   0.0% 100.0%      1.4   3.5% brpc::SpanQueue::Push
     0.0   0.0% 100.0%      1.9   4.8% butil::ObjectPool
     0.0   0.0% 100.0%      0.8   2.0% butil::ResourcePool
     0.0   0.0% 100.0%      1.0   2.6% butil::iobuf::tls_block
     0.0   0.0% 100.0%      1.0   2.6% bthread::TimerThread::Bucket::schedule
     0.0   0.0% 100.0%      1.6   4.1% bthread::get_stack
     0.0   0.0% 100.0%      4.2  10.8% bthread_id_create
     0.0   0.0% 100.0%      1.1   2.9% bvar::Variable::describe_series_exposed
     0.0   0.0% 100.0%      1.0   2.6% bvar::detail::AgentGroup
     0.0   0.0% 100.0%      0.5   1.3% bvar::detail::Percentile::operator
     0.0   0.0% 100.0%      0.5   1.3% bvar::detail::PercentileSamples
     0.0   0.0% 100.0%      0.5   1.3% bvar::detail::Sampler::schedule
     0.0   0.0% 100.0%      6.5  16.8% leveldb::Arena::AllocateNewBlock
     0.0   0.0% 100.0%      0.5   1.3% leveldb::VersionSet::LogAndApply
     0.0   0.0% 100.0%      4.2  10.8% pthread_mutex_unlock
     0.0   0.0% 100.0%      0.5   1.3% pthread_once
     0.0   0.0% 100.0%      0.5   1.3% std::_Rb_tree
     0.0   0.0% 100.0%      1.5   3.9% std::basic_string
     0.0   0.0% 100.0%      3.5   9.0% std::string::_Rep::_S_create
```

brpc还提供一个类似的growth profiler分析内存的分配去向（不考虑释放）。 

![img](../images/growth_profiler.png)

# MacOS的额外配置

1. 安装[standalone pprof](https://github.com/google/pprof)，并把下载的pprof二进制文件路径写入环境变量GOOGLE_PPROF_BINARY_PATH中
2. 安装llvm-symbolizer（将函数符号转化为函数名），直接用brew安装即可：`brew install llvm`

# Jemalloc Heap Profiler

## 开启方法

1. 编译[jemalloc](https://github.com/jemalloc/jemalloc)时需--enable-prof以支持profiler, 安装完成后bin目录下会有jeprof文件。
2. 启动进程前最好配置env `JEPROF_FILE=/xxx/jeprof`，否则进程默认用$PATH里的jeprof解析。
3. 进程开启profiler：
  - 启动进程并开启profiler功能：`MALLOC_CONF="prof:true" LD_PRELOAD=/xxx/lib/libjemalloc.so ./bin/test_server`，MALLOC_CONF是env项，prof:true只做一些初始化动作，并不会采样，但[prof_active](https://jemalloc.net/jemalloc.3.html#opt.prof_active)默认是true，所以进程启动就会采样。
  - 若静态链接jemalloc：`MALLOC_CONF="prof:true" ./bin/test_server`。
  - 或通过下面的gflags控制，gflags不会反应MALLOC_CONF值。
4. 相关gflags说明：
  - FLAGS_je_prof_active：true:开启采样，false:关闭采样。
  - FLAGS_je_prof_dump：修改值会生成heap文件，用于手动操作jeprof分析。
  - FLAGS_je_prof_reset：清理已采样数据和重置profiler选项，并且动态设置采样率，[默认](https://jemalloc.net/jemalloc.3.html#opt.lg_prof_sample)2^19B（512K），对性能影响可忽略。
5. 若要做memory leak:
  - `MALLOC_CONF="prof:true,prof_leak:true,prof_final:true" LD_PRELOAD=/xxx/lib/libjemalloc.so ./bin/test_server` ，进程退出时生成heap文件。
  - 注：可`kill pid`优雅退出，不可`kill -9 pid`；可用`FLAGS_graceful_quit_on_sigterm=true FLAGS_graceful_quit_on_sighup=true`来支持优雅退出。

注：
  - 每次dump的都是从采样至今的所有数据，若触发了reset，接来下dump的是从reset至今的所有数据，方便做diff。
  - 更多jemalloc profiler选项请参考[官网](https://jemalloc.net/jemalloc.3.html)，如`prof_leak_error:true`则检测到内存泄漏，进程立即退出。

## 样例

- jeprof命令`jeprof ip:port/pprof/heap`。

![img](../images/cmd_jeprof_text.png)

- curl生成text格式`curl ip:port/pprof/heap?display=text`。

![img](../images/curl_jeprof_text.png)

- curl生成svg图片格式`curl ip:port/pprof/heap?display=svg`。

![img](../images/curl_jeprof_svg.png)

- curl生成火焰图`curl ip:port/pprof/heap?display=flamegraph`。需配置env FLAMEGRAPH_PL_PATH=/xxx/flamegraph.pl，[flamegraph](https://github.com/brendangregg/FlameGraph)

![img](../images/curl_jeprof_flamegraph.png)

- curl获取内存统计信息`curl ip:port/pprof/heap?display=stats&opts=Ja`或`curl ip:port/memory?opts=Ja`，更多opts请参考[opts](https://github.com/jemalloc/jemalloc/blob/dev/include/jemalloc/internal/stats.h#L9)。

![img](../images/je_stats_print.png)

 - 内存使用量可关注:
   1. jemalloc.stats下的
     - [resident](https://jemalloc.net/jemalloc.3.html#stats.resident)
     - [metadata](https://jemalloc.net/jemalloc.3.html#stats.metadata)
     - [allocated](https://jemalloc.net/jemalloc.3.html#stats.allocated)：jeprof分析的就是这部分内存。
     - [active](https://jemalloc.net/jemalloc.3.html#stats.active)：active - allocated ≈ unuse。
   2. stats.arenas下的：
     - [resident](https://jemalloc.net/jemalloc.3.html#stats.arenas.i.resident)
     - [pactive](https://jemalloc.net/jemalloc.3.html#stats.arenas.i.pactive)
     - [base](https://jemalloc.net/jemalloc.3.html#stats.arenas.i.base)：含义近似metadata
     - [small.allocated](https://jemalloc.net/jemalloc.3.html#stats.arenas.i.small.allocated)
     - [large.allocated](https://jemalloc.net/jemalloc.3.html#stats.arenas.i.large.allocated)：arena allocated ≈ small.allocated + large.allocated

