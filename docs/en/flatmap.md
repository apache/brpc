# NAME

FlatMap - Maybe the fastest hashmap, with tradeoff of space.

# EXAMPLE

`` c ++
#include <string>
#include <butil / logging.h>
#include <butil / containers / flat_map.h>

void flatmap_example() {
butil :: FlatMap <int, std :: string> map;
// bucket_count: initial count of buckets, big enough to avoid resize.
// load_factor: element_count * 100 / bucket_count, 80 as default.
int bucket_count = 1000;
int load_factor = 80;
map.init(bucket_count, load_factor);
map.insert(10, "hello");
map[20] = "world";
std::string* value = map.seek(20);
CHECK(value != NULL);

    CHECK_EQ(2UL, map.size());
    CHECK_EQ(0UL, map.erase(30));
    CHECK_EQ(1UL, map.erase(10));

    LOG(INFO) << "All elements of the map:";
    for (butil::FlatMap<int, std::string>::const_iterator it = map.begin(); it != map.end(); ++it) {
        LOG(INFO) << it->first << " : " << it->second;
    }
    map.clear();
    CHECK_EQ(0UL, map.size());
}
``

# DESCRIPTION

[FlatMap](https://github.com/brpc/brpc/blob/master/src/butil/containers/flat_map.h) may be the fastest hash table, but when the value is larger, it needs more Memory, it is most suitable for small dictionaries that require extremely fast lookups during the retrieval process.

Principle: Put the content of the first node in the open chain bucket directly into the bucket. Since in practice, most buckets have no or fewer conflicts, most operations only require one memory jump: access to the corresponding bucket through the hash value. Two or more elements in the bucket are still stored in the linked list. Because the buckets are independent of each other, the conflict of one bucket will not affect the other buckets, and the performance is very stable. In many cases, the search performance of FlatMap is close to that of native arrays.

# BENCHMARK

The following is a comparison between FlatMap and other key/value containers:

-[AlignHashMap](https://svn.baidu.com/app/ecom/nova/trunk/public/util/container/alignhash.h): Faster implementation in closed chain.
-[CowHashMap](https://svn.baidu.com/app/ecom/nova/trunk/afs/smalltable/cow_hash_map.hpp): The open chain hash table in smalltable, which is different from ordinary open chain, is with Copy -on-write logic.
-[std::map](http://www.cplusplus.com/reference/map/map/): non-hash table, usually a red-black tree.

``
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
``
# Overview of hashmaps

Hash table is the most commonly used data structure. Its basic principle is to disperse different keys into different intervals through [Calculate Hash Value](http://en.wikipedia.org/wiki/Hash_function). When searching The search interval can be quickly narrowed through the hash value of the key. Under the premise of using appropriate parameters, the hash table can map a key to a value in O(1) time most of the time. Like other algorithms, this "O(1)" varies greatly in different implementations. The implementation of hash table generally has two parts:

## Calculate the hash value (non-encrypted)

That is to say, the most common method of hashing the key is linear congruence, but a good hash algorithm (non-encrypted) must consider many factors:

-The result is certain.
-[Avalanche effect](http://en.wikipedia.org/wiki/Avalanche_effect): The change of one bit in the input should try to affect the change of all bits in the output.
-The output should be distributed as evenly in the value range as possible.
-Make full use of modern cpu features: block calculation, reduce branching, loop unrolling, etc.

Most of the hashing algorithms are only for a key and will not consume too much cpu. The impact mainly comes from the overall data distribution of the hash table. For engineers, which algorithm to choose depends on the practical effect, some of the simplest methods may have good results. The general algorithm can choose Murmurhash.

## Resolve conflicts

Hash values ​​may overlap, and conflict resolution is another key factor in hash table performance. Common conflict resolution methods are:

-Open hashing (open hashing, closed addressing): An open hash table is an array of linked lists, in which linked lists are generally called buckets. When several keys fall into the same bucket, do a linked list insertion. This is the most common structure and has many advantages: it occupies O(NumElement * (KeySize + ValueSize + SomePointers)), and the resize will not invalidate the previous key/value memory. The buckets are independent, the conflict of one bucket will not affect other buckets, the average search time is relatively stable, and independent buckets are also prone to high concurrency. The disadvantage is that at least two memory jumps are required: first jump to the bucket entry, and then jump to the first node in the bucket. For some very small tables, this problem is not obvious, because when the table is very small, the node memory is close, but when the table becomes larger, the memory access becomes more and more random. If there is about 50ns in one visit (about 2G frequency), the search time of open-chain hash is often more than 100ns. In the layer-by-layer ranking process on the search side, some hot-spot dictionaries may be looked up millions of times in one second, and open-chain hashing sometimes becomes a hot spot. Some product lines may also criticize the memory of the open chain hash, because each key/value pair requires additional pointers.
-[Closed hashing](http://en.wikipedia.org/wiki/Open_addressing)(closed hashing or open addressing): The original intention of the closed chain is to reduce memory jumps. The bucket is no longer a linked list entry, but only needs Record a pair of key/value and some tags. When the bucket is occupied, follow different detection methods until an empty bucket is found. For example, linear exploration is to find the next bucket, and secondary exploration is to search by displacement of 1, 2, 4, 9... squared numbers. The advantage is: when the table is very empty or there are few conflicts, the lookup only requires one memory access, and there is no need to manage the node memory pool. But that's it. This method brings more disadvantages: the number of buckets must be greater than the number of elements, and all the memory before resize fails, which is difficult to concurrency. The more important thing is the aggregation effect: when there are many elements in the area (more than 70 %, not too much), the actual buckets of a large number of elements and the buckets they should be in have a large displacement. This makes the main operation of the hash table to scan a large area of ​​memory to find the element, and the performance is unstable and unpredictable. Closed-chain hash tables are "fast" in many people's impressions, but they are often inferior to open-chain hash tables in complex applications, and may be orders of magnitude slower. There are some derivative versions of the closed chain that try to solve this problem, such as [Hopscotch hashing](http://en.wikipedia.org/wiki/Hopscotch_hashing).
-Mixed open chain and closed chain: Generally, a part of the bucket array is taken out as a space for accommodating conflicting elements, such as [Coalesced hashing](http://en.wikipedia.org/wiki/Coalesced_hashing), but this The structure does not solve the memory jump problem of the open chain, and the structure is much more complicated than the closed chain, and the engineering effect is not good.
-Multiple hashes: generally replace one hash table with multiple hash tables, and try another hash table when a conflict occurs (with another hash value). A typical example is [Cuckoo hashing](http://en.wikipedia.org/wiki/Cuckoo_hashing), this structure also does not solve the memory jump.