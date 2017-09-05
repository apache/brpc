# 背景

ELF(Essential/Extreme/Excellent Learning Framework) 框架的目标是为公司内外的大数据应用提供学习/挖掘算法开发支持。 平台主要包括数据迭代处理的框架支持，并行计算过程中的通信支持和用于存储大规模参数的分布式、快速、高可用参数服务器。目前应用于fcr-model，公有云bml，大数据实验室，语音技术部门等等。

# 改造方法

之前是基于[zeromq](http://zeromq.org/)封装的rpc，这次改用baidu-rpc。

# 结论

单个rpc-call以及单次请求所有rpc-call的提升非常显著，延时都缩短了**50%**以上，总训练的耗时除了ftrl-sync-no-shuffle提升不明显外，其余三个算法训练总体性能都有**15%**左右的提升。现在瓶颈在于计算逻辑，所以相对单个的rpc的提升没有那么多。该成果后续会推动凤巢模型训练的上线替换。详细耗时见下节。

# 性能对比报告

## 算法总耗时

### Ftrl算法

替换前：

任务：[37361.nmg01-hpc-mmaster01.nmg01.baidu.com](http://nmg01-hpc-mon01.nmg01.baidu.com:8090/job/i-37361/)

总耗时：2:4:39

替换后：

任务：[24715.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-24715/)

总耗时：1:42:48

总体时间提升18%

### Ftrl-sync-no-shuffle算法

替换前：

任务：[16520.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-16520/)

总耗时：3:20:47

替换后：

任务：[24146.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-24146/)

总耗时：3:15:28

总时间提升2.5%

### Ftrl-sync算法

替换前：

任务：[18404.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-18404/)

总耗时：4:28:47

替换后

任务：[24718.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-24718/)

总耗时：3:45:57

总时间提升：16%

### Fm-sync算法

替换前

任务：[16587.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-16587/)

总耗时：6:16:37

替换后

任务：[24720.nmg01-hpc-imaster01.nmg01.baidu.com](http://nmg01-hpc-controller.nmg01.baidu.com:8090/job/i-24720/)

总耗时：5:21:00

总时间提升14.6%

## 子任务耗时

### 单个rpc-call

针对ftrl算法

|      | Average   | Max       | Min        |
| ---- | --------- | --------- | ---------- |
| 替换前  | 164.946ms | 7938.76ms | 0.249756ms |
| 替换后  | 10.4198ms | 2614.38ms | 0.076416ms |
| 缩短   | 93%       | 67%       | 70%        |

### 单次请求所有rpc-call

针对ftrl算法

|      | Average  | Max      | Min       |
| ---- | -------- | -------- | --------- |
| 替换前  | 658.08ms | 7123.5ms | 1.88159ms |
| 替换后  | 304.878  | 2571.34  | 0         |
| 缩短   | 53.7%    | 63.9%    |           |

## cpu profiling结果

top 40没有rpc相关函数。

```
Total: 8664 samples
     755   8.7%   8.7%      757   8.7% baidu::elf::Partitioner
     709   8.2%  16.9%      724   8.4% baidu::elf::GlobalShardWriterClient::local_aggregator::{lambda#1}::operator [clone .part.1341]
     655   7.6%  24.5%      655   7.6% boost::detail::lcast_ret_unsigned
     582   6.7%  31.2%      642   7.4% baidu::elf::RobinHoodLinkedHashMap
     530   6.1%  37.3%     2023  23.4% std::vector
     529   6.1%  43.4%      529   6.1% std::binary_search
     406   4.7%  48.1%      458   5.3% tc_delete
     405   4.7%  52.8%     2454  28.3% baidu::elf::GlobalShardWriterClient
     297   3.4%  56.2%      297   3.4% __memcpy_sse2_unaligned
     256   3.0%  59.2%      297   3.4% tc_new
     198   2.3%  61.5%      853   9.8% std::__introsort_loop
     157   1.8%  63.3%      157   1.8% baidu::elf::GrowableArray
     152   1.8%  65.0%      177   2.0% calculate_grad
     142   1.6%  66.7%      699   8.1% baidu::elf::BlockTableReaderManager
     137   1.6%  68.2%      656   7.6% baidu::elf::DefaultWriterServer
     127   1.5%  69.7%      127   1.5% _init
     122   1.4%  71.1%      582   6.7% __gnu_cxx::__normal_iterator
     117   1.4%  72.5%      123   1.4% baidu::elf::GrowableArray::emplace_back
     116   1.3%  73.8%      116   1.3% baidu::elf::RobinHoodHashMap::insert
     101   1.2%  75.0%      451   5.2% baidu::elf::NoCacheReaderClient
      99   1.1%  76.1%     3614  41.7% parse_ins
      97   1.1%  77.2%       97   1.1% std::basic_string::_Rep::_M_dispose [clone .part.12]
      96   1.1%  78.3%      154   1.8% std::basic_string
      91   1.1%  79.4%      246   2.8% boost::algorithm::split_iterator
      87   1.0%  80.4%      321   3.7% boost::function2
      76   0.9%  81.3%      385   4.4% boost::detail::function::functor_manager
      69   0.8%  82.1%       69   0.8% std::locale::~locale
      63   0.7%  82.8%      319   3.7% std::__unguarded_linear_insert
      58   0.7%  83.5%     2178  25.2% boost::algorithm::split [clone .constprop.2471]
      54   0.6%  84.1%      100   1.2% std::vector::_M_emplace_back_aux
      49   0.6%  84.7%       49   0.6% boost::algorithm::detail::is_any_ofF
      47   0.5%  85.2%       79   0.9% baidu::elf::DefaultReaderServer
      41   0.5%  85.7%       41   0.5% std::locale::_S_initialize
      39   0.5%  86.1%      677   7.8% boost::detail::function::function_obj_invoker2
      39   0.5%  86.6%       39   0.5% memset
      39   0.5%  87.0%       39   0.5% std::locale::locale
      38   0.4%  87.5%       50   0.6% FTRLAggregator::serialize
      36   0.4%  87.9%       67   0.8% tcmalloc::CentralFreeList::ReleaseToSpans
      34   0.4%  88.3%       34   0.4% madvise
      34   0.4%  88.7%       38   0.4% tcmalloc::CentralFreeList::FetchFromOneSpans
      32   0.4%  89.0%       32   0.4% std::__insertion_sort
```