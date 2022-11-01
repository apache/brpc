# background

ELF (Essential/Extreme/Excellent Learning Framework) framework provides learning/mining algorithm development support for big data applications inside and outside the company. The platform mainly includes framework support for iterative data processing, communication support during parallel computing, and a distributed, fast, and highly available parameter server for storing large-scale parameters. Used in fcr-model, public cloud bml, big data laboratory, voice technology department, etc. Previously it was based on the rpc packaged by [zeromq](http://zeromq.org/), this time I use brpc instead.

# in conclusion

The improvement of a single rpc-call and all rpc-calls in a single request is very significant, and the delay is shortened by more than 50%. The total training time is not significantly improved except for ftrl-sync-no-shuffle. The other three The overall performance of each algorithm training has been improved by about 15%. Now the bottleneck lies in the calculation logic, so the improvement relative to a single rpc is not so much. This achievement will promote the online replacement of the Phoenix Nest model training in the future. See the next section for detailed time-consuming.

# Performance comparison report

## Algorithm time-consuming

ftrl algorithm: the total time before replacement is 2:4:39, the total time after replacement is 1:42:48, an increase of 18%

ftrl-sync-no-shuffle algorithm: the total time before replacement 3:20:47, the total time after replacement 3:15:28, an increase of 2.5%

ftrl-sync algorithm: the total time before replacement 4:28:47, the total time after replacement 3:45:57, an increase of 16%

fm-sync algorithm: the total time before replacement is 6:16:37, the total time after replacement is 5:21:00, an increase of 14.6%

## Subtask time consuming

Single rpc-call (for ftrl algorithm)

| | Average | Max | Min |
| ---- | --------- | --------- | ---------- |
| Before Replacement | 164.946ms | 7938.76ms | 0.249756ms |
| After Replacement | 10.4198ms | 2614.38ms | 0.076416ms |
| Shorten | 93% | 67% | 70% |

Request all rpc-calls at a time (for ftrl algorithm)

| | Average | Max | Min |
| ---- | -------- | -------- | --------- |
| Before replacement | 658.08ms | 7123.5ms | 1.88159ms |
| After Replacement | 304.878 | 2571.34 | 0 |
| Shorten | 53.7% | 63.9% | |

## in conclusion

The improvement of a single rpc-call and all rpc-calls of a single request is very significant, the improvement is more than 50%, the total task time is not obvious except for the ftrl-sync-no-shuffle improvement, the other three algorithms have 15% The improvement of left and right, the bottleneck of the algorithm now lies in the calculation logic, so the improvement of a single rpc is not so much.

With cpu profiling results, top 40 has no rpc related functions.
```
Total: 8664 samples
     755 8.7% 8.7% 757 8.7% baidu::elf::Partitioner
     709 8.2% 16.9% 724 8.4% baidu::elf::GlobalShardWriterClient::local_aggregator::{lambda#1}::operator [clone .part.1341]
     655 7.6% 24.5% 655 7.6% boost::detail::lcast_ret_unsigned
     582 6.7% 31.2% 642 7.4% baidu::elf::RobinHoodLinkedHashMap
     530 6.1% 37.3% 2023 23.4% std::vector
     529 6.1% 43.4% 529 6.1% std::binary_search
     406 4.7% 48.1% 458 5.3% tc_delete
     405 4.7% 52.8% 2454 28.3% baidu::elf::GlobalShardWriterClient
     297 3.4% 56.2% 297 3.4% __memcpy_sse2_unaligned
     256 3.0% 59.2% 297 3.4% tc_new
     198 2.3% 61.5% 853 9.8% std::__introsort_loop
     157 1.8% 63.3% 157 1.8% baidu::elf::GrowableArray
     152 1.8% 65.0% 177 2.0% calculate_grad
     142 1.6% 66.7% 699 8.1% baidu::elf::BlockTableReaderManager
     137 1.6% 68.2% 656 7.6% baidu::elf::DefaultWriterServer
     127 1.5% 69.7% 127 1.5% _init
     122 1.4% 71.1% 582 6.7% __gnu_cxx::__normal_iterator
     117 1.4% 72.5% 123 1.4% baidu::elf::GrowableArray::emplace_back
     116 1.3% 73.8% 116 1.3% baidu::elf::RobinHoodHashMap::insert
     101 1.2% 75.0% 451 5.2% baidu::elf::NoCacheReaderClient
      99 1.1% 76.1% 3614 41.7% parse_ins
      97 1.1% 77.2% 97 1.1% std::basic_string::_Rep::_M_dispose [clone .part.12]
      96 1.1% 78.3% 154 1.8% std::basic_string
      91 1.1% 79.4% 246 2.8% boost::algorithm::split_iterator
      87 1.0% 80.4% 321 3.7% boost::function2
      76 0.9% 81.3% 385 4.4% boost::detail::function::functor_manager
      69 0.8% 82.1% 69 0.8% std::locale::~locale
      63 0.7% 82.8% 319 3.7% std::__unguarded_linear_insert
      58 0.7% 83.5% 2178 25.2% boost::algorithm::split [clone .constprop.2471]
      54 0.6% 84.1% 100 1.2% std::vector::_M_emplace_back_aux
      49 0.6% 84.7% 49 0.6% boost::algorithm::detail::is_any_ofF
      47 0.5% 85.2% 79 0.9% baidu::elf::DefaultReaderServer
      41 0.5% 85.7% 41 0.5% std::locale::_S_initialize
      39 0.5% 86.1% 677 7.8% boost::detail::function::function_obj_invoker2
      39 0.5% 86.6% 39 0.5% memset
      39 0.5% 87.0% 39 0.5% std::locale::locale
      38 0.4% 87.5% 50 0.6% FTRLAggregator::serialize
      36 0.4% 87.9% 67 0.8% tcmalloc::CentralFreeList::ReleaseToSpans
      34 0.4% 88.3% 34 0.4% madvise
      34 0.4% 88.7% 38 0.4% tcmalloc::CentralFreeList::FetchFromOneSpans
      32 0.4% 89.0% 32 0.4% std::__insertion_sort
```