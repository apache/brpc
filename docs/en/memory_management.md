Memory management is always an important part of the program. In the era of multithreading, a good memory allocation is mostly a trade-off between the following two points:

-Less competition between threads. The granularity of memory allocation is mostly small and sensitive to performance. If different threads compete for the same resource or the same lock during most allocations, the performance will be very bad. The reason is nothing more than cache consistency. Proof of the malloc program.
-Less wasted space. If each thread applies for each, the speed may be good, but if one thread always applies and the other thread always releases, the memory will explode. Memory is always shared between threads, and how to share is the key to the solution.

General applications can use mature memory allocation schemes such as [tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html), [jemalloc](https://github.com/jemalloc/jemalloc) But this is not enough for lower-level applications that focus on the long tail of performance. Multithreading frameworks widely pass the ownership of objects to make the problem asynchronous. How to make the cost of allocating these small objects smaller is a problem worthy of research. One of the characteristics is more significant:

-Most structures are of equal length.

This attribute can greatly simplify the process of memory allocation and obtain more stable and faster performance than general malloc. ResourcePool<T> and ObjectPool<T> in brpc provide this type of allocation.

> This article does not encourage users to use ResourcePool<T> or ObjectPool<T>. In fact, we oppose users to use these two classes in programs. Because the side effect of "equal length" is that a certain type monopolizes a part of the memory, which can no longer be used by other types. If abuse is not controlled, it will generate a large number of isolated memory allocation systems in the program, which is a waste of memory and There is not necessarily a better performance.

# ResourcePool<T>

Create an object of type T and return an offset, which can be converted into an object pointer in O(1) time. This offset is equivalent to a pointer, but its value is generally less than 2^32, so we can use it as part of a 64-bit id. The object can be returned, but after the return, the object is not deleted, nor is it destructed, but only enters the freelist. You may get this used object next time you apply, and you need to reset it before you can use it. When the object is returned, the object can still be accessed through the corresponding offset, that is, ResourcePool is only responsible for memory allocation and does not solve the ABA problem. But for offsets that are out of bounds, ResourcePool will return null.

Because the objects are of equal length, ResourcePool allocates and returns memory in batches to avoid global competition and reduce single-time overhead. The allocation process of each thread is as follows:

1. Check thread-local free block. If there are free objects, return. If not, step 2.
2. Try to fetch a free block from the global, if fetched, go back to step 1, otherwise step 3.
3. Take a block from the global and return the first object in it.

The principle is relatively simple. The data structure, atomic variables, memory fence and other issues will be more complicated in project implementation. The following uses the bthread_t generation process to illustrate how ResourcePool is applied.

# ObjectPool<T>

This is a variant of ResourcePool<T>, which does not return the offset, but directly returns the object pointer. The internal structure is similar to ResourcePool, and some codes are simpler. For users, this is a multi-threaded object pool, which is also used in brpc. For example, in Socket::Write, each request to be written is packaged as WriteRequest, and this object is allocated with ObjectPool<WriteRequest>.

# Generate bthread_t

Users expect to obtain a higher degree of concurrency by creating bthreads, so creating bthreads must be fast. In the current implementation, the average time to create a bthread is less than 200ns. If you have to create it from scratch every time, it can't be so fast. The creation process is more like taking an instance from a bthread pool, and we also need an id to refer to a bthread, so here is where ResourcePool comes in. bthread is called Task in the code, and its structure is called TaskMeta, which is defined in [task_meta.h](https://github.com/brpc/brpc/blob/master/src/bthread/task_meta.h), All TaskMeta are allocated by ResourcePool<TaskMeta>.

Most functions of bthread need to access TaskMeta through bthread_t in O(1) time, and when bthread_t fails, the access should return NULL to allow the function to make a return error. The solution is: bthread_t consists of a 32-bit version and a 32-bit offset. The version solves [ABA problem](http://en.wikipedia.org/wiki/ABA_problem), and the offset is allocated by ResourcePool<TaskMeta>. When searching, the TaskMeta is obtained by the offset first, and then the version is checked. If the version does not match, it means that the bthread is invalid. Note: This is just a rough idea. In a multithreaded environment, even if the versions are equal, bthreads may still fail at any time. Different bthread functions have different processing methods. Some functions will be locked, and some can tolerate unequal versions. .

![img](../images/resource_pool.png)

This id generation method is widely used in brpc. SocketId and bthread_id_t in brpc are also allocated in a similar way.

# Stack

The side effect of using ResourcePool to speed up creation is: the stack of all bthreads in a pool must be the same size. This seems to limit the user's choice, but based on our observations, most users do not care about the specific size of the stack, and only need two sizes of stacks: normal size but small number, small size but large number. So we use different pools to manage stacks of different sizes, and users can choose according to the scene. The two stacks correspond to the attributes BTHREAD_ATTR_NORMAL (the stack defaults to 1M) and BTHREAD_ATTR_SMALL (the stack defaults to 32K). The user can also specify BTHREAD_ATTR_LARGE. The stack size of this attribute is the same as that of pthread. Due to the larger size, bthread will not cache it and the creation speed is slower. The server uses BTHREAD_ATTR_NORMAL to run user code by default.

The stack is allocated using [mmap](http://linux.die.net/man/2/mmap), and bthread will also use mprotect to allocate a 4K guard page to detect stack overflow. Since mmap+mprotect cannot exceed max_map_count (the default is 65536), this parameter may need to be adjusted when there are too many bthreads. In addition, when there are many bthreads, the memory problem may not only be the stack, but also various user and system buffers.

Before 1.3, goroutine used [segmented stacks](https://gcc.gnu.org/wiki/SplitStacks) to dynamically adjust the stack size and found that there is [hot split](https://docs.google.com/document/d /1wAaf1rYoM4S4gtnPh0zOlGzWtrZFQ5suE8qr2sD8uWQ/pub) problem was changed to a variable-length continuous stack (similar to vector resizing, only suitable for memory managed languages). Since bthread is basically only used on 64-bit platforms, the virtual memory space is huge, and the demand for variable-length stacks is not clear. In addition, the performance of segmented stacks has an impact, and bthread has no plans to change the stack length for the time being.