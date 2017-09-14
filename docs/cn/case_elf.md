# 背景

ELF(Essential/Extreme/Excellent Learning Framework) 框架为公司内外的大数据应用提供学习/挖掘算法开发支持。 平台主要包括数据迭代处理的框架支持，并行计算过程中的通信支持和用于存储大规模参数的分布式、快速、高可用参数服务器。应用于fcr-model，公有云bml，大数据实验室，语音技术部门等等。之前是基于[zeromq](http://zeromq.org/)封装的rpc，这次改用brpc。

# 结论

单个rpc-call以及单次请求所有rpc-call的提升非常显著，延时都缩短了**50%**以上，总训练的耗时除了ftrl-sync-no-shuffle提升不明显外，其余三个算法训练总体性能都有**15%**左右的提升。现在瓶颈在于计算逻辑，所以相对单个的rpc的提升没有那么多。该成果后续会推动凤巢模型训练的上线替换。详细耗时见下节。

# 性能对比报告

## 算法总耗时

ftrl算法: 替换前总耗时2:4:39, 替换后总耗时1:42:48, 提升18%

ftrl-sync-no-shuffle算法: 替换前总耗时3:20:47, 替换后总耗时3:15:28, 提升2.5%

ftrl-sync算法: 替换前总耗时4:28:47, 替换后总耗时3:45:57, 提升16%

fm-sync算法: 替换前总耗时6:16:37, 替换后总耗时5:21:00, 提升14.6%

## 子任务耗时

单个rpc-call(针对ftrl算法)

|      | Average   | Max       | Min        |
| ---- | --------- | --------- | ---------- |
| 替换前  | 164.946ms | 7938.76ms | 0.249756ms |
| 替换后  | 10.4198ms | 2614.38ms | 0.076416ms |
| 缩短   | 93%       | 67%       | 70%        |

单次请求所有rpc-call(针对ftrl算法)

|      | Average  | Max      | Min       |
| ---- | -------- | -------- | --------- |
| 替换前  | 658.08ms | 7123.5ms | 1.88159ms |
| 替换后  | 304.878  | 2571.34  | 0         |
| 缩短   | 53.7%    | 63.9%    |           |

## 结论

单个rpc-call以及单次请求所有rpc-call的提升非常显著，提升都在50%以上，总任务的耗时除了ftrl-sync-no-shuffle提升不明显外，其余三个算法都有15%左右的提升，现在算法的瓶颈在于对计算逻辑，所以相对单个的rpc的提升没有那么多。

附cpu profiling结果, top 40没有rpc相关函数。
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
