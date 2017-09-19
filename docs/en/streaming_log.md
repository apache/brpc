# Name

streaming_log - Print log to std::ostreams

# SYNOPSIS

```c++
#include <butil/logging.h>

LOG(FATAL) << "Fatal error occurred! contexts=" << ...;
LOG(WARNING) << "Unusual thing happened ..." << ...;
LOG(TRACE) << "Something just took place..." << ...;
LOG(TRACE) << "Items:" << noflush;
LOG_IF(NOTICE, n > 10) << "This log will only be printed when n > 10";
PLOG(FATAL) << "Fail to call function setting errno";
VLOG(1) << "verbose log tier 1";
CHECK_GT(1, 2) << "1 can't be greater than 2";
 
LOG_EVERY_SECOND(INFO) << "High-frequent logs";
LOG_EVERY_N(ERROR, 10) << "High-frequent logs";
LOG_FIRST_N(INFO, 20) << "Logs that prints for at most 20 times";
LOG_ONCE(WARNING) << "Logs that only prints once";
```

# DESCRIPTION

Streaming log is the best choice for printing complex objects or template objects. As most objects are complicate, user needs to convert all the fields to string first in order to use `printf` with `%s`. However it's very inconvenient (can't append numbers) and needs lots of temporary memory (caused by string). The solution in C++ is to send the log as a stream to the `std::ostream` object. For example, in order to print object A, we need to implement the following interface:

```c++
std::ostream& operator<<(std::ostream& os, const A& a);
```

The signature of the function means to print object `a` to `os` and then return `os`. The return value of `os` enables us to combine binary operator `<<` (left-combine). As a result, `os << a << b << c;` means  `operator<<(operator<<(operator<<(os, a), b), c);`. Apparently `operator<<` needs a returning reference to complete this process, which is also called chaining. In languages that don't support operator overloading, you will see a more tedious form, such as `os.print(a).print(b).print(c)`.

You should also use chaining in your own implementation of `operator<<`. In fact, printing a complex object is like DFS a  tree: Call `operator<<` on each child node, and then each child node invokes the function on the grandchild node, and so forth. For example, object A has two member variables: B and C. Printing A becomes the process of putting B and C ostream:

```c++
struct A {
    B b;
    C c;
};
std::ostream& operator<<(std::ostream& os, const A& a) {
    return os << "A{b=" << a.b << ", c=" << a.c << "}";
}
```

Data structure of B and C along with the print function：

```c++
struct B {
    int value;
};
std::ostream& operator<<(std::ostream& os, const B& b) {
    return os << "B{value=" << b.value << "}";
}
 
struct C {
    string name;
};
std::ostream& operator<<(std::ostream& os, const C& c) {
    return os << "C{name=" << c.name << "}";
}
```

Finally the result of printing object A is:

```
A{b=B{value=10}, c=C{name=tom}}
```

This way we don't need to allocate temporary memory since objects are directly passed into the ostream object. Of course, the memory management of ostream itself is another topic.

OK, now we connect the whole printing process by ostream. The most common ostream objects are  `std::cout` and `std::cerr`, so objects implement the above function can be directly sent to `std::cout` and `std::cerr`. In other words, if a log stream also inherits ostream, then these objects can be written into log. Streaming log is such a log stream that inherits `std::ostream` to send the object into the log. In the current implementation, the logs are recorded in a thread-local buffer, which will be flushed into screen or ` logging::LogSink` after a complete log record. Of course, the implementation is thread safe.

## LOG

If you have ever used glog before, you should find it easy to start. The log macro is the same as glog. For example, to print a FATAL log (Note that there is no `std::endl`):

```c++
LOG(FATAL) << "Fatal error occurred! contexts=" << ...;
LOG(WARNING) << "Unusual thing happened ..." << ...;
LOG(TRACE) << "Something just took place..." << ...;
```

The log level of streaming log in accordance with glog：

| streaming log | glog                 | Use Cases                                |
| ------------- | -------------------- | ---------------------------------------- |
| FATAL         | FATAL (coredump)     | Fatal error. Since most fatal log inside baidu is not fatal actually, it won't trigger coredump directly as glog, unless you turn on [-crash_on_fatal_log](http://brpc.baidu.com:8765/flags/crash_on_fatal_log) |
| ERROR         | ERROR                | Non-fatal error.                         |
| WARNING       | WARNING              | Unusual branches                         |
| NOTICE        | -                    | Generally you should not use NOTICE as it's intended for important business logs. Make sure to check with other developers. glog doesn't have NOTICE. |
| INFO, TRACE   | INFO                 | Important side effects such as open/close some resources. |
| VLOG(n)       | INFO                 | Detailed log that support multiple layers. |
| DEBUG         | INFOVLOG(1) (NDEBUG) | Just for compatibility. Print logs only when `NDEBUG` is not defined. See DLOG/DPLOG/DVLOG for more reference. |

## PLOG

The difference of PLOG and LOG is that it will append error information at the end of log. It's kind of  like `%m` in `printf`. Under POSIX environment, the error code is `errno`。

```c++
int fd = open("foo.conf", O_RDONLY);   // foo.conf does not exist, errno was set to ENOENT
if (fd < 0) { 
    PLOG(FATAL) << "Fail to open foo.conf";    // "Fail to open foo.conf: No such file or directory"
    return -1;
}
```

## noflush

If you don't want to flush the log at once, append `noflush`. It's commonly used inside a loop:

```c++
LOG(TRACE) << "Items:" << noflush;
for (iterator it = items.begin(); it != items.end(); ++it) {
    LOG(TRACE) << ' ' << *it << noflush;
}
LOG(TRACE);
```

The first two LOG(TRACE) doesn't flush the log to the screen. They are recorded inside the thread-local buffer. The third LOG(TRACE) flush all logs into the screen. If there are 3 elements inside items and we don't append `noflush`, the result would be:

```
TRACE: ... Items:
TRACE: ...  item1
TRACE: ...  item2
TRACE: ...  item3
```

After we add `noflush`:

```
TRACE: ... Items: item1 item2 item3 
```

The `noflush` feature also support bthread so that we can push lots of logs from the server's bthreads without actually print them (using `noflush`), and flush the whole log at the end of RPC. Note that you should not use `noflush` when implementing an asynchronous method since it will change the underlying bthread, leaving `noflush` out of function.

## LOG_IF

`LOG_IF(log_level, condition)` prints only when condition is true. It's the same as `if (condition) { LOG() << ...; }` with shorter code：

```c++
LOG_IF(NOTICE, n > 10) << "This log will only be printed when n > 10";
```

## XXX_EVERY_SECOND

XXX represents for LOG, LOG_IF, PLOG, SYSLOG, VLOG, DLOG, and so on. These logging macros print log at most once per second. You can use these to check running status inside hotspot area. The first call to this macro prints the log immediately, and costs additional 30ns (caused by gettimeofday) compared to normal LOG.

```c++
LOG_EVERY_SECOND(INFO) << "High-frequent logs";
```

## XXX_EVERY_N

XXX represents for LOG, LOG_IF, PLOG, SYSLOG, VLOG, DLOG, and so on. These logging macros print log every N times. You can use these to check running status inside hotspot area. The first call to this macro prints the log immediately, and costs an additional atomic operation (relaxed order) compared to normal LOG. This macro is thread safe which means counting from multiple threads is also accurate while glog is not.

```c++
LOG_EVERY_N(ERROR, 10) << "High-frequent logs";
```

## XXX_FIRST_N

XXX represents for LOG, LOG_IF, PLOG, SYSLOG, VLOG, DLOG, and so on. These logging macros print log at most N times. It costs an additional atomic operation (relaxed order) compared to normal LOG before N, and zero cost after.

```c++
LOG_FIRST_N(ERROR, 20) << "Logs that prints for at most 20 times";
```

## XXX_ONCE

XX represents for LOG, LOG_IF, PLOG, SYSLOG, VLOG, DLOG, and so on. These logging macros print log at most once. It's the same as `XXX_FIRST_N(..., 1)`

```c++
LOG_ONCE(ERROR) << "Logs that only prints once";
```

## VLOG

VLOG(verbose_level) is detail log that support multiple layers. It uses 2 gflags: *--verbose* and *--verbose_module* to control the logging layer you want (Note that glog uses *--v* and *--vmodule*). The log will be printed only when `--verbose` >= `verbose_level`: 

```c++
VLOG(0) << "verbose log tier 0";
VLOG(1) << "verbose log tier 1";
VLOG(2) << "verbose log tier 2";
```

When `--verbose=1`, the first 2 log will be printed while the last won't. Module means a file or file path without the extension name, and value of `--verbose_module` will overwrite `--verbose`. For example:

```bash
--verbose=1 --verbose_module="channel=2,server=3"    # print VLOG of those with verbose value:
                                                     # channel.cpp <= 2
                                                     # server.cpp <= 3
                                                     # other files <= 1
--verbose=1 --verbose_module="src/brpc/channel=2,server=3"
                                                    # For files with same names, add paths
```

You can set `--verbose` and `--verbose_module` through `google::SetCommandLineOption` dynamically.

VLOG has another form VLOG2, which allows user to specify virtual path:

```c++
// public/foo/bar.cpp
VLOG2("a/b/c", 2) << "being filtered by a/b/c rather than public/foo/bar";
```

> VLOG and VLOG2 also have corresponding VLOG_IF and VLOG2_IF.

## DLOG

All log macros have debug versions, starting with D, such as DLOG, DVLOG. When NDEBUG is defined, these logs will not be printed.

**Do not put important side effects inside the log streams beginning with D.**

*No printing* means that even the parameters are not evaluated. If your parameters have side effects, they won't happend when NDEBUG is defined. For example, `DLOG(FATAL) << foo();` where foo is a function or it changes a dictionary, anyway, it's essential. However, it won't be evaluated when NDEBUG is defined.

## CHECK

Another import variation of logging is `CHECK(expression)`. When expression evaluates to false, it will print a fatal log. It's kind of like `ASSERT` in gtest, and has other form such as CHECK_EQ, CHECK_GT, and so on. When check fails, the message after will be printed.

```c++
CHECK_LT(1, 2) << "This is definitely true, this log will never be seen";
CHECK_GT(1, 2) << "1 can't be greater than 2";
```

Run the above code you should see a fatal log and the calling stack：

```
FATAL: ... Check failed: 1 > 2 (1 vs 2). 1 can't be greater than 2
#0 0x000000afaa23 butil::debug::StackTrace::StackTrace()
#1 0x000000c29fec logging::LogStream::FlushWithoutReset()
#2 0x000000c2b8e6 logging::LogStream::Flush()
#3 0x000000c2bd63 logging::DestroyLogStream()
#4 0x000000c2a52d logging::LogMessage::~LogMessage()
#5 0x000000a716b2 (anonymous namespace)::StreamingLogTest_check_Test::TestBody()
#6 0x000000d16d04 testing::internal::HandleSehExceptionsInMethodIfSupported<>()
#7 0x000000d19e96 testing::internal::HandleExceptionsInMethodIfSupported<>()
#8 0x000000d08cd4 testing::Test::Run()
#9 0x000000d08dfe testing::TestInfo::Run()
#10 0x000000d08ec4 testing::TestCase::Run()
#11 0x000000d123c7 testing::internal::UnitTestImpl::RunAllTests()
#12 0x000000d16d94 testing::internal::HandleSehExceptionsInMethodIfSupported<>()
```

The second column of the callstack is the address of the code segment. You can use `addr2line` to check the corresponding file and line:

```
$ addr2line -e ./test_base 0x000000a716b2 
/home/gejun/latest_baidu_rpc/public/common/test/test_streaming_log.cpp:223
```

You **should** use `CHECK_XX` for arithmetic condition so that you can see more detailed information when check failed.

```c++
int x = 1;
int y = 2;
CHECK_GT(x, y);  // Check failed: x > y (1 vs 2).
CHECK(x > y);    // Check failed: x > y.
```

Like DLOG, you should NOT include important side effects inside DCHECK.

## LogSink

The default destination of streaming log is the screen. You can change it through `logging::SetLogSink`. Users can inherit LogSink and implement their own output logic. We provide an internal LogSink as an example:

### StringSink

Inherit both LogSink and string. Store log content inside string and mainly aim for unit test. The following case shows a classic usage of StringSink:

```c++
TEST_F(StreamingLogTest, log_at) {
    ::logging::StringSink log_str;
    ::logging::LogSink* old_sink = ::logging::SetLogSink(&log_str);
    LOG_AT(FATAL, "specified_file.cc", 12345) << "file/line is specified";
    // the file:line part should be using the argument given by us.
    ASSERT_NE(std::string::npos, log_str.find("specified_file.cc:12345"));
    // restore the old sink.
    ::logging::SetLogSink(old_sink);
}
```

