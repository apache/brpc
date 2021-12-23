# Quick introduction

```c++
#include <bvar/bvar.h>

namespace foo {
namespace bar {

// bvar::Adder<T> is used for accumulation. The following defines an Adder that counts the total number of read errors.
bvar::Adder<int> g_read_error;
// Put bvar::Window on other bvars to get the value in the time window.
bvar::Window<bvar::Adder<int>> g_read_error_minute("foo_bar", "read_error", &g_read_error, 60);
// ^ ^ ^
// 60 seconds for the prefix monitoring item name, 10 seconds for ignoring

// bvar::LatencyRecorder is a compound variable that can count: total, qps, average delay, delay quantile, maximum delay.
bvar::LatencyRecorder g_write_latency("foo_bar", "write");
// ^ ^
// Prefix monitoring items, don't add latency! LatencyRecorder contains multiple bvars, which will have their own suffixes, such as write_qps, write_latency, etc.

// Define a variable that counts the number of "tasks that have been pushed".
bvar::Adder<int> g_task_pushed("foo_bar", "task_pushed");
// Put bvar::PerSecond on other bvars to get the *average per second* value in the time window, here is the number of tasks pushed in per second.
bvar::PerSecond<bvar::Adder<int>> g_task_pushed_second("foo_bar", "task_pushed_second", &g_task_pushed);
// ^ ^
// Unlike Window, PerSecond will be divided by the size of the time window. The time window is the last parameter, which is not filled here, which is 10 seconds by default.

} // bar
} // foo
```

Where it is applied:

```c++
// encountered a read error
foo::bar::g_read_error << 1;

// write_latency is 23ms
foo::bar::g_write_latency << 23;

// Push in 1 task
foo::bar::g_task_pushed << 1;
```

Note that Window<> and PerSecond<> are both derived variables and will be automatically updated, so you don't need to push values ​​for them. Of course, you can also use bvar as a member variable or local variable.

Commonly used bvars are:

-`bvar::Adder<T>`: counter, default 0, varname << N is equivalent to varname += N.
-`bvar::Maxer<T>`: Find the maximum value, the default is std::numeric_limits<T>::min(), varname << N is equivalent to varname = max(varname, N).
-`bvar::Miner<T>`: Find the minimum value, default std::numeric_limits<T>::max(), varname << N is equivalent to varname = min(varname, N).
-`bvar::IntRecorder`: Find the average value since use. Note that the attributive here is not "in a period of time". Generally, the average value in the time window is derived through Window.
-`bvar::Window<VAR>`: Get the accumulated value of a bvar over a period of time. Window is derived from the existing bvar and will be updated automatically.
-`bvar::PerSecond<VAR>`: Obtain the average accumulated value of a bvar per second in a period of time. PerSecond is also a derivative variable that is automatically updated.
-`bvar::LatencyRecorder`: Variables dedicated to recording latency and qps. Input delay, average delay/maximum delay/qps/total times are all available.

** Make sure that the variable name is globally unique! ** Otherwise, the exposure will fail. If -bvar_abort_on_same_name is true, the program will abort directly.

There are different bvars from various modules in the program. To avoid duplication of names, it is recommended to name them like this: **module_class name_index**

-**Module** is generally the name of the program, and abbreviations for product lines can be added, such as inf_ds, ecom_retrbs, etc.
-**Class name** is generally a class name or function name, such as storage_manager, file_transfer, rank_stage1, etc.
-**Indicators** are generally count, qps, latency.

Some correct names are as follows:

```
iobuf_block_count: 29 # module=iobuf class name=block index=count
iobuf_block_memory: 237568 # Module=iobuf class name=block indicator=memory
process_memory_resident: 34709504 # module=process class name=memory indicator=resident
process_memory_shared: 6844416 # module=process class name=memory indicator=shared
rpc_channel_connection_count: 0 # module=rpc class name=channel_connection indicator=count
rpc_controller_count: 1 # module=rpc class name=controller indicator=count
rpc_socket_count: 6 # module=rpc class name=socket indicator=count
```

Currently bvar will do name normalization, no matter what you type is foo::BarNum, foo.bar.num, foo bar num, foo-bar-num, the final result is foo_bar_num.

About indicators:

-The number is suffixed with _count, such as request_count, error_count.
-The number per second is suffixed with _second, such as request_second, process_inblocks_second, it is clear enough, and there is no need to write _count_second or _per_second.
-The number per minute is suffixed with _minute, such as request_minute, process_inblocks_minute

If you need to use a counter defined in another file, you need to declare the corresponding variable in the header file.

```c++
namespace foo {
namespace bar {
// Note that g_read_error_minute and g_task_pushed_second are derived bvars, which will be updated automatically, do not declare them.
extern bvar::Adder<int> g_read_error;
extern bvar::LatencyRecorder g_write_latency;
extern bvar::Adder<int> g_task_pushed;
} // bar
} // foo
```

**Do not define global Window or PerSecond across files**. The initialization order of global variables in different compilation units is [undefined](https://isocpp.org/wiki/faq/ctors#static-init-order). Defining `Adder<int> foo_count` in foo.cpp, and `PerSecond<Adder<int>> foo_qps(&foo_count);` in foo_qps.cpp is an **error** approach.

About thread-safety:

-bvar is thread compatible. You can operate different bvars in different threads. For example, you can expose or hide **different **bvars in multiple threads at the same time. They will reasonably operate the global data that needs to be shared, which is safe.
-**Except for the read-write interface**, other functions of bvar are thread-unsafe: For example, you cannot expose or hide the same **bvar in multiple threads at the same time, which may cause the program to crash. Generally speaking, there is no need for other interfaces besides reading and writing to operate in multiple threads at the same time.

Time can use butil::Timer, the interface is as follows:

```c++
#include <butil/time.h>
namespace butil {
class Timer {
public:
    enum TimerType {STARTED };

    Timer();

    // butil::Timer tm(butil::Timer::STARTED); // tm is already started after creation.
    explicit Timer(TimerType);

    // Start this timer
    void start();

    // Stop this timer
    void stop();

    // Get the elapse from start() to stop().
    int64_t n_elapsed() const; // in nanoseconds
    int64_t u_elapsed() const; // in microseconds
    int64_t m_elapsed() const; // in milliseconds
    int64_t s_elapsed() const; // in seconds
};
} // namespace butil
```

# bvar::Variable

Variable is the base class of all bvars. It mainly provides functions such as global registration, enumeration, and query.

When the user creates a bvar with default parameters, the bvar is not registered in any global structure. In this case, the bvar is purely a faster counter. We call the act of registering a bvar in the global table "exposure", which can be exposed through the `expose` function:
```c++
// Expose this variable globally so that it's counted in following functions:
// list_exposed
// count_exposed
// describe_exposed
// find_exposed
// Return 0 on success, -1 otherwise.
int expose(const butil::StringPiece& name);
int expose_as(const butil::StringPiece& prefix, const butil::StringPiece& name);
```
The bvar name after global exposure is name or prefix + name, which can be queried through the static function suffixed with _exposed. For example, Variable::describe_exposed(name) will return the description of the bvar named name.

When a bvar with the same name already exists, expose will print the FATAL log and return -1. If the option **-bvar_abort_on_same_name** is set to true (default is false), the program will abort directly.

Here are some examples of exposure of bvar:
```c++
bvar::Adder<int> count1;

count1 << 10 << 20 << 30; // values ​​add up to 60.
count1.expose("count1"); // expose the variable globally
CHECK_EQ("60", bvar::Variable::describe_exposed("count1"));
count1.expose("another_name_for_count1"); // expose the variable with another name
CHECK_EQ("", bvar::Variable::describe_exposed("count1"));
CHECK_EQ("60", bvar::Variable::describe_exposed("another_name_for_count1"));

bvar::Adder<int> count2("count2"); // exposed in constructor directly
CHECK_EQ("0", bvar::Variable::describe_exposed("count2")); // default value of Adder<int> is 0

bvar::Status<std::string> status1("count2", "hello"); // the name conflicts. if -bvar_abort_on_same_name is true,
                                                       // program aborts, otherwise a fatal log is printed.
```

In order to avoid duplication, the name of bvar should be prefixed with `<namespace>_<module>_<name>`. For ease of use, we provide the **expose_as** function, which receives a prefix.
```c++
// Expose this variable with a prefix.
// Example:
// namespace foo {
// namespace bar {
// class ApplePie {
// ApplePie() {
// // foo_bar_apple_pie_error
// _error.expose_as("foo_bar_apple_pie", "error");
//}
// private:
// bvar::Adder<int> _error;
// };
//} // foo
//} // bar
int expose_as(const butil::StringPiece& prefix, const butil::StringPiece& name);
```

# Export all variables

The most common export requirement is to query and write local files through the HTTP interface. The former is provided through the [/vars](vars.md) service in brpc, while the latter has been implemented in bvar and is not turned on by default. There are several ways to turn on this feature:

-Use [gflags](flags.md) to parse the input parameters, and add -bvar_dump when the program is started, or in brpc, you can also dynamically modify it after startup through the [/flags](flags.md) service. The parsing method of gflags is as follows, add the following code to the main function:

```c++
  #include <gflags/gflags.h>
  ...
  int main(int argc, char* argv[]) {
      google::ParseCommandLineFlags(&argc, &argv, true/* means to delete the recognized parameters from argc/argv*/);
      ...
  }
```

-I don't want to use gflags to parse the parameters, but I want to open it directly in the program by default, and add the following code to the main function:

```c++
#include <gflags/gflags.h>
...
int main(int argc, char* argv[]) {
    if (google::SetCommandLineOption("bvar_dump", "true").empty()) {
        LOG(FATAL) << "Fail to enable bvar dump";
    }
    ...
}
```

The dump function is controlled by the following gflags:

| Name | Default value | Function |
| ------------------ | ----------------------- | ------ ---------------------------------- |
| bvar_dump | false | Create a background thread dumping all bvar periodically, all bvar_dump_* flags are not effective when this flag is off |
| bvar_dump_exclude | "" | Dump bvar excluded from these wildcards(separated by comma), empty means no exclusion |
| bvar_dump_file | monitor/bvar.\<app\>.data | Dump bvar into this file |
| bvar_dump_include | "" | Dump bvar matching these wildcards(separated by comma), empty means including all |
| bvar_dump_interval | 10 | Seconds between consecutive dump |
| bvar_dump_prefix | \<app\> | Every dumped name starts with this prefix |
| bvar_dump_tabs | \<check the code\> | Dump bvar into different tabs according to the filters (seperated by semicolon), format: *(tab_name=wildcards) |

When bvar_dump_file is not empty, the program will start a background export thread to update bvar_dump_file at the interval specified by bvar_dump_interval, which contains all bvars matched by bvar_dump_include and not matched by bvar_dump_exclude.

For example, we modify all gflags to the following figure:

![img](../images/bvar_dump_flags_2.png)

The export file is:

```
$ cat bvar.echo_server.data
rpc_server_8002_builtin_service_count: 20
rpc_server_8002_connection_count: 1
rpc_server_8002_nshead_service_adaptor: brpc::policy::NovaServiceAdaptor
rpc_server_8002_service_count: 1
rpc_server_8002_start_time: 2015/07/24-21:08:03
rpc_server_8002_uptime_ms: 14740954
```

Like "`iobuf_block_count: 8`" is filtered by bvar_dump_include, "`rpc_server_8002_error: 0`" is excluded by bvar_dump_exclude.

If your program does not use brpc, you still need to dynamically modify gflag (generally not needed), you can call google::SetCommandLineOption(), as shown below:
```c++
#include <gflags/gflags.h>
...
if (google::SetCommandLineOption("bvar_dump_include", "*service*").empty()) {
    LOG(ERROR) << "Fail to set bvar_dump_include";
    return -1;
}
LOG(INFO) << "Successfully set bvar_dump_include to *service*";
```
Do not directly set FLAGS_bvar_dump_file / FLAGS_bvar_dump_include / FLAGS_bvar_dump_exclude.
On the one hand, these gflag types are all std::string, and direct overwriting is thread-unsafe; on the other hand, the validator (callback to check correctness) will not be triggered, so the background export thread will not be started.

Users can also use the dump_exposed function to customize how to export all exposed bvars in the process:
```c++
// Implement this class to write variables into different places.
// If dump() returns false, Variable::dump_exposed() stops and returns -1.
class Dumper {
public:
    virtual bool dump(const std::string& name, const butil::StringPiece& description) = 0;
};

// Options for Variable::dump_exposed().
struct DumpOptions {
    // Contructed with default options.
    DumpOptions();
    // If this is true, string-type values ​​will be quoted.
    bool quote_string;
    // The? In wildcards. Wildcards in URL need to use another character
    // because? is reserved.
    char question_mark;
    // Separator for white_wildcards and black_wildcards.
    char wildcard_separator;
    // Name matched by these wildcards (or exact names) are kept.
    std::string white_wildcards;
    // Name matched by these wildcards (or exact names) are skipped.
    std::string black_wildcards;
};

class Variable {
    ...
    ...
    // Find all exposed variables matching `white_wildcards' but
    // `black_wildcards' and send them to `dumper'.
    // Use default options when `options' is NULL.
    // Return number of dumped variables, -1 on error.
    static int dump_exposed(Dumper* dumper, const DumpOptions* options);
};
```

# bvar::Reducer

Reducer uses binary operators to combine multiple values ​​into one value. The operators must satisfy the associative law, commutative law, and no side effects. Only by satisfying these three points can we ensure that the result of the merge is not affected by how the thread private data is distributed. Like subtraction does not satisfy the associative and commutative laws, it cannot be used as the operator here.
```c++
// Reduce multiple values ​​into one with `Op': e1 Op e2 Op e3 ...
// `Op' shall satisfy:
//-associative: a Op (b Op c) == (a Op b) Op c
//-commutative: a Op b == b Op a;
//-no side effects: a Op b never changes if a and b are fixed.
// otherwise the result is undefined.
template <typename T, typename Op>
class Reducer: public Variable;
```
The function of reducer << e1 << e2 << e3 is equivalent to reducer = e1 op e2 op e3.

Common Redcuer subclasses are bvar::Adder, bvar::Maxer, bvar::Miner.

## bvar::Adder

As the name implies, it is used for accumulation, and Op is +.
```c++
bvar::Adder<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(2, value.get_value());

bvar::Adder<double> fp_value; // There may be warning
fp_value << 1.0 << 2.0 << 3.0 << -4.0;
CHECK_DOUBLE_EQ(2.0, fp_value.get_value());
```
Adder<> can be used for non-basic types, and the corresponding type must at least overload `T operator+(T, T)`. An existing example is std::string. The following code will splice the strings together:
```c++
// This is just proof-of-concept, don't use it for production code because it makes a
// bunch of temporary strings which is not efficient, use std::ostringstream instead.
bvar::Adder<std::string> concater;
std::string str1 = "world";
concater << "hello "<< str1;
CHECK_EQ("hello world", concater.get_value());
```

## bvar::Maxer
Used to get the maximum value, the operator is std::max.
```c++
bvar::Maxer<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(3, value.get_value());
```
Since Maxer<> use std::numeric_limits<T>::min() as the identity, it cannot be applied to generic types unless you specialized std::numeric_limits<> (and overloaded operator<, yes, not operator>).

## bvar::Miner

Used to take the minimum value, the operator is std::min.
```c++
bvar::Maxer<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(-4, value.get_value());
```
Since Miner<> use std::numeric_limits<T>::max() as the identity, it cannot be applied to generic types unless you specialized std::numeric_limits<> (and overloaded operator<).

# bvar::IntRecorder

Used to calculate the average value.
```c++
// For calculating average of numbers.
// Example:
// IntRecorder latency;
// latency << 1 << 3 << 5;
// CHECK_EQ(3, latency.average());
class IntRecorder: public Variable;
```

# bvar::LatencyRecorder

A counter dedicated to calculating latency and qps. Just fill in the latency data to get the latency / max_latency / qps / count. The statistics window is the last parameter, which is not filled in as bvar_dump_interval (not filled here).

Note: LatencyRecorder does not inherit Variable, but a combination of multiple bvars.
```c++
LatencyRecorder write_latency("table2_my_table_write"); // produces 4 variables:
                                                         // table2_my_table_write_latency
                                                         // table2_my_table_write_max_latency
                                                         // table2_my_table_write_qps
                                                         // table2_my_table_write_count
// In your write function
write_latency << the_latency_of_write;
```

# bvar::Window

Obtain the statistical value of the previous period of time. Window cannot exist independently, and must rely on an existing counter. Window will update automatically, without sending data to it. For performance reasons, Window's data comes from sampling the original counter once per second. In the worst case, Window's return value has a 1 second delay.
```c++
// Get data within a time window.
// The time unit is 1 second fixed.
// Window relies on other bvar which should be constructed before this window and destructs after this window.
// R must:
//-have get_sampler() (not require thread-safe)
//-defined value_type and sampler_type
template <typename R>
class Window: public Variable;
```

# bvar::PerSecond

Obtain the average statistic value per second in the previous period of time. It is basically the same as Window, except that the return value will be divided by the time window.
```c++
bvar::Adder<int> sum;

// sum_per_second.get_value() is the accumulated value of sum *average per second* in the previous 60 seconds. If the last time window is omitted, it will default to bvar_dump_interval.
bvar::PerSecond<bvar::Adder<int>> sum_per_second(&sum, 60);
```
**PerSecond doesn't always make sense**

There is no Maxer in the above code, because the maximum value in a period of time divided by the time window is meaningless.
```c++
bvar::Maxer<int> max_value;

// Mistake! The maximum value divided by the time is meaningless
bvar::PerSecond<bvar::Maxer<int>> max_value_per_second_wrong(&max_value);

// Correct, the correct approach is to set the Window's time window to 1 second
bvar::Window<bvar::Maxer<int>> max_value_per_second(&max_value, 1);
```

## Difference with Window

For example, if you want to count the changes in the memory in the last minute, if you use Window<>, the return value means "the memory has increased by 18M in the last minute", and if you use PerSecond<>, the return value means "average every last minute The second increased by 0.3M".

The advantage of Window is that it is accurate and suitable for some relatively small quantities, such as "the number of errors in the last minute". If PerSecond is used, the result may be "0.0167 errors per second in the last minute". Compared with "There was 1 error in the last minute" is obviously not clear enough. Other time-independent quantities also need to use Window. For example, the method of calculating the cpu occupancy rate of the last minute is to use an Adder to accumulate the cpu time and the real time at the same time, and then use the Window to obtain the last minute of the cpu time and the real time, both Divide and get the CPU occupancy rate of the last minute, which has nothing to do with time. Using PerSecond will produce wrong results.

# bvar::Status

Record and display a value, with additional set_value function.
```c++

// Display a rarely or periodically updated value.
// Usage:
// bvar::Status<int> foo_count1(17);
// foo_count1.expose("my_value");
//
// bvar::Status<int> foo_count2;
// foo_count2.set_value(17);
//
// bvar::Status<int> foo_count3("my_value", 17);
//
// Notice that Tp needs to be std::string or acceptable by boost::atomic<Tp>.
template <typename Tp>
class Status: public Variable;
```

# bvar::PassiveStatus

Display the value on demand. In some occasions, we cannot set_value or don't know the frequency of set_value. A more suitable way may be to print it when it needs to be displayed. The user passes in the print callback function to achieve this purpose.
```c++
// Display a updated-by-need value. This is done by passing in an user callback
// which is called to produce the value.
// Example:
// int print_number(void* arg) {
// ...
// return 5;
//}
//
// // number1: 5
// bvar::PassiveStatus status1("number1", print_number, arg);
//
// // foo_number2: 5
// bvar::PassiveStatus status2(typeid(Foo), "number2", print_number, arg);
template <typename Tp>
class PassiveStatus: public Variable;
```
Although very simple, PassiveStatus is one of the most useful bvars, because many statistics already exist, we don't need to store them again, but just get them on demand. For example, the following code declares a bvar that displays the user name of the process under Linux:
```c++

static void get_username(std::ostream& os, void*) {
    char buf[32];
    if (getlogin_r(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf)-1] ='\0';
        os << buf;
    } else {
        os << "unknown";
    }
}
PassiveStatus<std::string> g_username("process_username", get_username, NULL);
```

# bvar::GFlag

Expose important gflags as bvar so that they're monitored (in noah).
```c++
DEFINE_int32(my_flag_that_matters, 8, "...");

// Expose the gflag as *same-named* bvar so that it's monitored (in noah).
static bvar::GFlag s_gflag_my_flag_that_matters("my_flag_that_matters");
// ^
// the gflag name

// Expose the gflag as a bvar named "foo_bar_my_flag_that_matters".
static bvar::GFlag s_gflag_my_flag_that_matters_with_prefix("foo_bar", "my_flag_that_matters");
```