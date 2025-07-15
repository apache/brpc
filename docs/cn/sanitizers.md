# Sanitizers

新版本的GCC/Clang支持[sanitizers](https://github.com/google/sanitizers)，方便开发者排查代码中的bug。 bRPC对sanitizers提供了一定的支持。

## AddressSanitizer(ASan)

ASan提供了[对协程的支持](https://reviews.llvm.org/D20913)。 在bthread创建、切换、销毁时，让ASan知道当前bthread的栈信息，主要用于维护[fake stack](https://github.com/google/sanitizers/wiki/AddressSanitizerUseAfterReturn)。

bRPC中启用ASan的方法：给config_brpc.sh增加`--with-asan`选项、给cmake增加`-DWITH_ASAN=ON`选项或者给bazel增加`--define with_asan=true`选项。

另外需要注意的是，ASan没法检测非ASan分配内存或者对象池复用内存。所以我们封装了两个宏，让ASan知道内存块是否能被使用。在非ASan环境下，这两个宏什么也不做，没有开销。

```c++
#include <butil/debug/address_annotations.h>

BUTIL_ASAN_POISON_MEMORY_REGION(addr, size);
BUTIL_ASAN_UNPOISON_MEMORY_REGION(addr, size);
```

如果某些对象池在设计上允许操作对象池中的对象，例如ExecutionQueue、Butex，则需要特化ObjectPoolWithASanPoison，表示不对这些对象池的对象内存进行poison/unpoison，例如：

```c++
namespace butil {
// TaskNode::cancel() may access the TaskNode object returned to the ObjectPool<TaskNode>,
// so ObjectPool<TaskNode> can not poison the memory region of TaskNode.
template <>
struct ObjectPoolWithASanPoison<bthread::TaskNode> : false_type {};
} // namespace butil

namespace butil {
// Butex object returned to the ObjectPool<Butex> may be accessed,
// so ObjectPool<Butex> can not poison the memory region of Butex.
template <>
struct ObjectPoolWithASanPoison<bthread::Butex> : false_type {};
} // namespace butil
```

其他问题：如果ASan报告中new/delete的调用栈不完整，可以通过设置`fast_unwind_on_malloc=0`回溯出完整的调用栈了。需要注意的是`fast_unwind_on_malloc=0`很耗性能。

## ThreadSanitizer(TSan)

待支持（TODO）

## LeakSanitizer(LSan) / MemorySanitizer(MSan) / UndefinedBehaviorSanitizer(UBSan) 

bRPC默认支持这三种sanitizers，编译及链接时加上对应的选项即可启用对应的sanitizers：

1. LSan: `-fsanitize=leak`；
2. MSan: `-fsanitize=memory`；
3. UBSan: `-fsanitize=undefined`；
