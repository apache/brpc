Generally there are three ways of IO operations:

- blocking IO: after the IO operation is issued, the current thread is blocked until the process of IO ends, which is a kind of synchronous IO, such as the default action of posix [read](http://linux.die.net/man/2/read) and [write](http://linux.die.net/man/2/write).
- non-blocking IO: If there is nothing to read or overcrowded to write, the API will return immediately with an error code. Non-blocking IO is often used with [poll](http://linux.die.net/man/2/poll), [select](http://linux.die.net/man/2/select), [epoll](http://linux.die.net/man/4/epoll) in Linux or [kqueue](https://www.freebsd.org/cgi/man.cgi?query=kqueue&sektion=2) in BSD.
- asynchronous IO: you call an API to start a read/write operation, and the framework calls you back when it is done, such as [OVERLAPPED](https://msdn.microsoft.com/en-us/library/windows/desktop/ms684342(v=vs.85).aspx) + [IOCP](https://msdn.microsoft.com/en-us/library/windows/desktop/aa365198(v=vs.85).aspx) in Windows. Native AIO in Linux is only supported for files.

Non-blocking IO is usually used to increabse IO concurrency in Linux. When the IO concurrency is low, non-blocking IO is not necessarily more efficient than blocking IO, since blocking IO is handled completely by the kernel and system calls like read/write are highly optimized which are apparently more effective.

# Receiving messages

A message is a fix-length binary data read from a connection, which may come from the request from upstream client or the reponse from downstream server. Brpc uses one or serveral [EventDispatcher](https://github.com/brpc/brpc/blob/master/src/brpc/event_dispatcher.cpp)(referred to as EDISP) waiting for events from any fd. Unlike the common IO threads, EDISP is not responsible for reading or writing. The problem of IO threads is that one thread can only read one fd at a given time, so some read requests may starve when many budy fds are assigned to one IO thread. Features like multi-tenant, flow scheduling and [Streaming RPC](streaming_rpc.md) will aggravate the problem. The occasional long delayed read at high load also slows down the reading of all fds in an IO thread, which has a great impact on usability.


