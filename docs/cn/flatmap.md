# NAME

FlatMap - Maybe the fastest hashmap, with tradeoff of space.

# EXAMPLE

`#include <butil/containers/flat_map.h>`

# DESCRIPTION

[FlatMap](https://github.com/brpc/brpc/blob/master/src/butil/containers/flat_map.h)可能是最快的哈希表，但当value较大时它需要更多的内存，它最适合作为检索过程中需要极快查找的小字典。

原理：把开链桶中第一个节点的内容直接放桶内。由于在实践中，大部分桶没有冲突或冲突较少，所以大部分操作只需要一次内存跳转：通过哈希值访问对应的桶。桶内两个及以上元素仍存放在链表中，由于桶之间彼此独立，一个桶的冲突不会影响其他桶，性能很稳定。在很多时候，FlatMap的查找性能和原生数组接近。

# BENCHMARK

下面是FlatMap和其他key/value容器的比较：

- [AlignHashMap](https://svn.baidu.com/app/ecom/nova/trunk/public/util/container/alignhash.h)：闭链中较快的实现。
- [CowHashMap](https://svn.baidu.com/app/ecom/nova/trunk/afs/smalltable/cow_hash_map.hpp)：smalltable中的开链哈希表，和普通开链不同的是带Copy-on-write逻辑。
- [std::map](http://www.cplusplus.com/reference/map/map/)：非哈希表，一般是红黑树。

```
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 8 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 15/19/30/102ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 7/11/33/146ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 10/28/26/93ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 6/9/29/100ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 10/21/26/130ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 5/10/30/104ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 32 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 23/31/31/130ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 9/11/72/104ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 20/53/28/112ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 7/10/29/101ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 20/46/28/137ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 7/10/29/112ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 128 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 34/109/91/179ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 8/11/33/112ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 28/76/86/169ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 8/9/30/110ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Sequentially inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 28/68/87/201ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Sequentially erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 9/9/30/125ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 8 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 14/56/29/157ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 9/11/31/181ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 11/17/27/156ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 6/10/30/204ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 13/26/27/212ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 7/11/38/309ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 32 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 24/32/32/181ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 10/12/32/182ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 21/46/35/168ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 7/10/36/209ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 24/46/31/240ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 8/11/40/314ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:474] [ value = 128 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting   100 into FlatMap/AlignHashMap/CowHashMap/std::map takes 36/114/93/231ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing     100 from FlatMap/AlighHashMap/CowHashMap/std::map takes 9/12/35/190ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting  1000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 44/94/88/224ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing    1000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 8/10/34/236ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:521] Randomly inserting 10000 into FlatMap/AlignHashMap/CowHashMap/std::map takes 46/92/93/314ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:558] Randomly erasing   10000 from FlatMap/AlighHashMap/CowHashMap/std::map takes 12/11/42/362ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 8 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/7/12/54ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 3/7/11/78ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/8/13/172ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 32 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 5/8/12/55ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/8/11/82ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 6/10/14/164ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 128 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 7/9/13/56ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 6/10/12/93ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 9/12/21/166ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 8 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/7/11/56ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 3/7/11/79ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/9/13/173ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 32 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 5/8/12/54ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 4/8/11/100ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 6/10/14/165ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:576] [ value = 128 bytes ]
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking   100 from FlatMap/AlignHashMap/CowHashMap/std::map takes 7/9/12/56ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking  1000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 6/10/12/88ns
TRACE: 12-30 13:19:53:   * 0 [test_flat_map.cpp:637] Seeking 10000 from FlatMap/AlignHashMap/CowHashMap/std::map takes 9/14/20/169ns
```
# Overview of hashmaps

哈希表是最常用的数据结构，它的基本原理是通过[计算哈希值](http://en.wikipedia.org/wiki/Hash_function)把不同的key分散到不同的区间，在查找时通过key的哈希值能快速地缩小查找区间。在使用恰当参数的前提下，哈希表在大部分时候能在O(1)时间内把一个key映射为value。像其他算法一样，这个“O(1)”在不同的实现中差异很大。哈希表的实现一般有两部分：

## 计算哈希值(非加密型)

即把key散列开的方法，最常见的莫过于线性同余，但一个好的哈希算法(非加密型)要考虑很多因素：

- 结果是确定的。
- [雪崩效应](http://en.wikipedia.org/wiki/Avalanche_effect)：输入中一个bit的变化应该尽量影响输出所有bit的变化。
- 输出应尽量在值域中均匀分布。
- 充分利用现代cpu特性：成块计算，减少分支，循环展开等等。

大部分哈希算法针对的只是一个key，不会耗用太多的cpu。影响主要来自哈希表的整体数据分布，对于工程师来说，选用何种算法得看实践效果，一些最简单的方法也许就有很好的效果。通用算法可选择Murmurhash。

## 解决冲突

哈希值可能重合，解决冲突是哈希表性能的另一关键因素。常见的冲突解决方法有：

- 开链哈希(open hashing, closed addressing): 开链哈希表是链表的数组，其中链表一般称为桶。当若干个key落到同一个桶时，做链表插入。这是最通用的结构，有很多优点：占用内存为O(NumElement * (KeySize + ValueSize + SomePointers))，resize时候不会使之前的存放key/value的内存失效。桶之间是独立的，一个桶的冲突不会影响到其他桶，平均查找时间较为稳定，独立的桶也易于高并发。缺点是至少要两次内存跳转：先跳到桶入口，再跳到桶中的第一个节点。对于一些很小的表这个问题不明显，因为当表很小时，节点内存是接近的，但当表变大时，访存就愈发随机。如果一次访存在50ns左右（2G左右主频），开链哈希的查找时间往往就在100ns以上。在检索端的层层ranking过程中，对一些热点字典的查找1秒内可能有几百万次以上，开链哈希有时会成为热点。一些产品线可能对开链哈希的内存也有诟病，因为每对key/value都需要额外的指针。
- [闭链哈希](http://en.wikipedia.org/wiki/Open_addressing)(closed hashing or open addressing): 闭链的初衷是减少内存跳转，桶不再是链表入口，而只需要记录一对key/value与一些标记，当桶被占时，按照不同的探查方法直到找到空桶为止。比如线性探查就是查找下一个桶，二次探查是按1,2,4,9...平方数位移查找。优点是：当表很空时或冲突较少时，查找只需要一次访存，也不需要管理节点内存池。但仅此而已，这个方法带来了更多缺点：桶个数必须大于元素个数，resize后之前的内存全部失效，难以并发. 更关键的是聚集效应：当区域内元素较多时（超过70%，其实不算多），大量元素的实际桶和它们应在的桶有较大位移。这使哈希表的主要操作都要扫过一大片内存才能找到元素，性能不稳定难以预测。闭链哈希表在很多人的印象中“很快”，但在复杂的应用中往往不如开链哈希表，并且可能是数量级的慢。闭链有一些衍生版本试图解决这个问题，比如[Hopscotch hashing](http://en.wikipedia.org/wiki/Hopscotch_hashing)。
- 混合开链和闭链：一般是把桶数组中的一部分拿出来作为容纳冲突元素的空间，典型如[Coalesced hashing](http://en.wikipedia.org/wiki/Coalesced_hashing)，但这种结构没有解决开链的内存跳转问题，结构又比闭链复杂很多，工程效果并不好。
- 多次哈希：一般用多个哈希表代替一个哈希表，当发生冲突时（用另一个哈希值）尝试另一个哈希表。典型如[Cuckoo hashing](http://en.wikipedia.org/wiki/Cuckoo_hashing)，这个结构也没有解决内存跳转。
