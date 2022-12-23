
# Quick introduction

The basic usage of bvar is simple:

```c++
#include <bvar/bvar.h>

namespace foo {
namespace bar {

// bvar::Adder<T> used for running sum, we define a Adder for read_error as below
bvar::Adder<int> g_read_error;

// put another bvar inside window so that we can get the value over this period of time
bvar::Window<bvar::Adder<int> > g_read_error_minute("foo_bar", "read_error", &g_read_error, 60);
//                                                     ^          ^                         ^
//                                                    prefix1     monitor name             time window, 10 by default

// bvar::LatencyRecorder is a compound varibale, can be used for troughput、qps、avg latency, latency percentile, max latency。
bvar::LatencyRecorder g_write_latency("foo_bar", "write");
//                                      ^          ^
//                                     prefix1      monitor entry, LatencyRecorder includes different bvar, and expose() will add the suffix for them by default, such as write_qps, write_latency etc

// define a varible for the # of 'been-pushed task'
bvar::Adder<int> g_task_pushed("foo_bar", "task_pushed");
// put nested bvar into PerSecond so that we can get the value per second within this time window. Over here what we get is the # of tasks pushed per second
bvar::PerSecond<bvar::Adder<int> > g_task_pushed_second("foo_bar", "task_pushed_second", &g_task_pushed);
//       ^                                                                                             ^
//    different from Window, PerSecond will be divided by the time winodw.                            time window is the last param, we omit here, its 10 by default

}  // bar
}  // foo
```

how we use the bvar
```c++
// run into read errors
foo::bar::g_read_error << 1;

// record down the latenct, which is 23ms
foo::bar::g_write_latency << 23;

// one task has been pushed
foo::bar::g_task_pushed << 1;
```
Remember Window<> and PerSecond<> are derived variables, we don't have to push value to them and they will auto-update.
Obviously, we can take bvar as local or member variables.

There are essentially 7 commonly used bvar classes, they all extend from the base class bvar:Variable.

-   `bvar::Adder<T>`  : counter, 0 by default, varname << N equals to varname += N。
-   `bvar::Maxer<T>`  : get the maximum value, std::numeric_limits::min() by default, varname << N equals to varname = max(varname, N)。
-   `bvar::Miner<T>`  : get the minimum value, std::numeric_limits::max() by default, varname << N equals to varname = min(varname, N)。
-   `bvar::IntRecorder`  : get the mean value since it was in use, notice here we don't use “over a period of time”, since this bvar always comes with Window<> to calculate the mean value within the predefined time window.
-   `bvar::Window<VAR>`  : get the running sum over a time window. Window derives from other existing bvar and will auto-update
-   `bvar::PerSecond<VAR>`  : get the value per second during a predefined amount of time. PerSecond will also auto-update and derives from other bvar
-   `bvar::LatencyRecorder`  : intended for recording latency and qps, when we push latency to it, for mean latency/max lantency/qps, we will get all in once.

**caveat: make sure the name of bvar is globally unique, otherwise, the expose() will fail. When the option -bvar_abort_on_same_name is true(false by default), program will abort.**

### Best Practice for Naming:
There are different bvar from different module, to avoid duplicating name, we'd better follow the rule as `module_class_indicator
-   a module usually refers to the program name, can be the acronym of product line, like inf_ds, ecom_retrbs etc.
-   a class usually refers to the class name/ function name, like storage_manager, file_transfer, rank_stage1.
-   an indicator usually refers to qps, count, latency etc.

some legit naming examples
```
iobuf_block_count : 29                          # module=iobuf   class=block  indicator=count
iobuf_block_memory : 237568                     # module=iobuf   class=block  indicator=memory
process_memory_resident : 34709504              # module=process class=memory indicator=resident
process_memory_shared : 6844416                 # module=process class=memory indicator=shared
rpc_channel_connection_count : 0                # module=rpc     class=channel_connection  indicator=count
rpc_controller_count : 1                        # module=rpc     class=controller indicator=count
rpc_socket_count : 6                            # module=rpc     class=socket     indicator=count
```
bvar will normalize the variable name, no matter whether we type foo::BarNum, foo.bar.num, foo bar num , foo-bar-num, they all go to foo_bar_num in the end.



**Things about indicators:**

-   use `_count` as suffix for number, like request_count, error_count
-   use `_second` as suffix for number per second is clear enough, no need to use '_count_second' or '_per_second', like request_second, process_inblocks_second
-   `_minute` as suffix for number per minute like request_minute, process_inblocks_minute

if we need to use a counter defined in another file, we have to declare that variable in header file
```
namespace foo {
namespace bar {
// notice g_read_error_minute and g_task_pushed_second are derived bvar, will auto update, no need to declare
extern bvar::Adder<int> g_read_error;
extern bvar::LatencyRecorder g_write_latency;
extern bvar::Adder<int> g_task_pushed;
}  // bar
}  // foo
```
**Don't define golabel Window<> and PerSecond<> across files. The order for the initialization of global variables in different compile units is undefined.** foo.cpp defines `Adder<int> foo_count`, then defining `PerSecond<Adder<int> > foo_qps(&foo_count);` in foo_qps.cpp is illegal

**Things about thread-safety**:

-   bvar is thread-compatible. We can manipulate a bvar in different threads, such as we can expose or hide different bvar in multiple threads simultaneously, they will safely do some job on global shared variables.
-   **Excpet read and write API,** any other functions of bvar are not thread-safe：u can not expose or hide a same bvar in different threads, this may cause the program crash. Generally speaking, we don't have to call any other API concurrently except read and write.

we can use butil::Timer for timer, API is as below:
```C++
#include <butil/time.h>
namespace butil {
class Timer {
public:
    enum TimerType { STARTED };

    Timer();

    // butil::Timer tm(butil::Timer::STARTED);  // tm is already started after creation.
    explicit Timer(TimerType);

    // Start this timer
    void start();

    // Stop this timer
    void stop();

    // Get the elapse from start() to stop().
    int64_t n_elapsed() const;  // in nanoseconds
    int64_t u_elapsed() const;  // in microseconds
    int64_t m_elapsed() const;  // in milliseconds
    int64_t s_elapsed() const;  // in seconds
};
}  // namespace butil
```


# bvar::Variable:

Varibale is the base class for all bvar, it provides registering, listing and searching functions.

When user created a bvar with default params, it hasn't been registered into any global structure, it's merely a faster counter, which means we cannot use it elsewhere. The action of putting this bvar into the global registry is called `expose`, can be achieved by calling `expose()`

The name for globally exposed bvar consists of 'name' or 'name+prefix', can be searched by function with suffix `_exposed` , e.g. Variable::describe_exposed("foo") will return the description of bvar with the name 'foo'.

When there already exists the name, expose() will print FATAL log and return -1. When the option **-bvar_abort_on_same_name** is true(false by default), program will abort.

Some examples for expose() as below
```
bvar::Adder<int> count1; // create a bvar with defalut params
count1 << 10 << 20 << 30;   // values add up to 60.
count1.expose("count1");  // expose the variable globally
CHECK_EQ("60", bvar::Variable::describe_exposed("count1"));
count1.expose("another_name_for_count1");  // expose the variable with another name
CHECK_EQ("", bvar::Variable::describe_exposed("count1"));
CHECK_EQ("60", bvar::Variable::describe_exposed("another_name_for_count1"));

bvar::Adder<int> count2("count2");  // exposed in constructor directly
CHECK_EQ("0", bvar::Variable::describe_exposed("count2"));  // default value of Adder<int> is 0

bvar::Status<std::string> status1("count2", "hello");  // the name conflicts. if -bvar_abort_on_same_name is true,
                                                      // program aborts, otherwise a fatal log is printed.
```
To avoid duplicate name, we should have prefix for bvar, we recommend the name as `<namespace>_<module>_<name>`

For convenience, we provide expose_as() as it will accept a prefix.
```C++
// Expose this variable with a prefix.
// Example:
//   namespace foo {
//   namespace bar {
//   class ApplePie {
//       ApplePie() {
//           // foo_bar_apple_pie_error
//           _error.expose_as("foo_bar_apple_pie", "error");
//       }
//   private:
//       bvar::Adder<int> _error;
//   };
//   }  // foo
//   }  // bar
int expose_as(const butil::StringPiece& prefix, const butil::StringPiece& name);
```
# Export All Variables

Common needs for exporting are querying by HTTP API and writing into local file, the former is provided by brpc [/vars](https://github.com/apache/brpc/blob/master/docs/cn/vars.md) service, the latter has been implemented in bvar, and it's turned off by default. A couple of methods can activate this function：

-   Using [gflags](https://github.com/apache/brpc/blob/master/docs/cn/flags.md) to parse the input params. We can add `-bvar_dump` during the starup of program or we can dynamically change the params thru brpc [/flags](https://github.com/apache/brpc/blob/master/docs/cn/flags.md) service after starup. gflags parsing is as below
    ```C++
    #include <gflags/gflags.h>
    ...
    int main(int argc, char* argv[]) {
        if (google::SetCommandLineOption("bvar_dump", "true").empty()) {
            LOG(FATAL) << "Fail to enable bvar dump";
        }
        ...
    }
    ```


-   If u dont want to use gflags and expect them opened by default in program
    ```C++
    #include <gflags/gflags.h>
    ...
    int main(int argc, char* argv[]) {
        if (google::SetCommandLineOption("bvar_dump", "true").empty()) {
            LOG(FATAL) << "Fail to enable bvar dump";
        }
        ...
    }
    
      ```

-   dump function is controlled by following gflags
    | Name                 | Default Value                     | Effect                                       |
    | ------------------ | ----------------------- | ---------------------------------------- |
    | bvar_dump          | false                   | Create a background thread dumping all bvar periodically, all bvar_dump_* flags are not effective when this flag is off |
    | bvar_dump_exclude  | ""                      | Dump bvar excluded from these wildcards(separated by comma), empty means no exclusion |
    | bvar_dump_file     | monitor/bvar.\<app\>.data | Dump bvar into this file                 |
    | bvar_dump_include  | ""                      | Dump bvar matching these wildcards(separated by comma), empty means including all |
    | bvar_dump_interval | 10                      | Seconds between consecutive dump         |
    | bvar_dump_prefix   | \<app\>                 | Every dumped name starts with this prefix |
    | bvar_dump_tabs     | \<check the code\>      | Dump bvar into different tabs according to the filters (seperated by semicolon), format: *(tab_name=wildcards) |


    when the bvar_dump_file is not empty, a background thread will be started to update `bvar_dump_file` for the specified time interval called `bvar_dump_interval` , including all the bvar which is matched by `bvar_dump_include` while not matched by `bvar_dump_exclude`
    
    such like we modify the gflags as below：
    
    [![img](https://github.com/apache/brpc/raw/master/docs/images/bvar_dump_flags_2.png)](https://github.com/apache/brpc/blob/master/docs/images/bvar_dump_flags_2.png)
    
    exporting file will be like：
    ```
    $ cat bvar.echo_server.data
    rpc_server_8002_builtin_service_count : 20
    rpc_server_8002_connection_count : 1
    rpc_server_8002_nshead_service_adaptor : brpc::policy::NovaServiceAdaptor
    rpc_server_8002_service_count : 1
    rpc_server_8002_start_time : 2015/07/24-21:08:03
    rpc_server_8002_uptime_ms : 14740954
    ```
    `iobuf_block_count : 8` is filtered out by bvar_dump_include, `rpc_server_8002_error : 0` is ruled out by bvar_dump_exclude.
    
    if you didn't use brpc in your program, u also need to dynamically change gflags（normally no need), we can call google::SetCommandLineOption(), as below
    ```c++
    #include <gflags/gflags.h>
    ...
    if (google::SetCommandLineOption("bvar_dump_include", "*service*").empty()) {
        LOG(ERROR) << "Fail to set bvar_dump_include";
        return -1;
    }
    LOG(INFO) << "Successfully set bvar_dump_include to *service*";
    ```
    Do not directly set FLAGS_bvar_dump_file / FLAGS_bvar_dump_include / FLAGS_bvar_dump_exclude. On the one hand, these gflags are std::string, which are not thread-safe to be overridden；On the other hand, the validator will not be triggered(call back to check the correctness), so the exporting thread will not be invoked
    
    users can also customize dump_exposed() to export all the exposed bvar：
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
        // If this is true, string-type values will be quoted.
        bool quote_string;
        // The ? in wildcards. Wildcards in URL need to use another character
        // because ? is reserved.
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

Reducer uses binary operators over several values to combine them into one final result, which are commutative, associative and without side effect. Only satisfying all of these thress, we can make sure the combined result is not affected by the distribution of the private variables of thread. Like substraciton does not satisfy associative nor commutative, so it cannot be taken as the operator here.
```C++
// Reduce multiple values into one with `Op': e1 Op e2 Op e3 ...
// `Op' shall satisfy:
//   - associative:     a Op (b Op c) == (a Op b) Op c
//   - commutative:     a Op b == b Op a;
//   - no side effects: a Op b never changes if a and b are fixed.
// otherwise the result is undefined.
template <typename T, typename Op>
class Reducer : public Variable;
```
reducer << e1 << e2 << e3 equals to reducer = e1 op e2 op e3。
Common Redcuer subclass: bvar::Adder, bvar::Maxer, bvar::Miner

## bvar::Adder

we can tell from its name, it's intended for running sum. Opeator is `+`
```c++
bvar::Adder<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(2, value.get_value());

bvar::Adder<double> fp_value;  // may have warning
fp_value << 1.0 << 2.0 << 3.0 << -4.0;
CHECK_DOUBLE_EQ(2.0, fp_value.get_value());
```


Adder<> can be applied to the non-primitive type, which at least overrides `T operator+(T, T)`, an existing example is `std::string`, the code below will concatenate strings：
```c++
// This is just proof-of-concept, don't use it for production code because it makes a
// bunch of temporary strings which is not efficient, use std::ostringstream instead.
bvar::Adder<std::string> concater;
std::string str1 = "world";
concater << "hello " << str1;
CHECK_EQ("hello world", concater.get_value());
```
## bvar::Maxer

is producing the maximum value, operator is `std::max` 。
```c++
bvar::Maxer<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(3, value.get_value());
```
Since Maxer<> use std::numeric_limits::min() as the identity, it cannot be applied to generic types unless you specialized std::numeric_limits<> (and overloaded operator<, yes, not operator>).

## bvar::Miner

producing minimum value, operator is std::min
```c++
bvar::Maxer<int> value;
value << 1 << 2 << 3 << -4;
CHECK_EQ(-4, value.get_value());
```
Since Miner<> use std::numeric_limits::max() as the identity, it cannot be applied to generic types unless you specialized std::numeric_limits<> (and overloaded operator<).

# bvar::IntRecorder

used for mean value
```c++
// For calculating average of numbers.
// Example:
//   IntRecorder latency;
//   latency << 1 << 3 << 5;
//   CHECK_EQ(3, latency.average());
class IntRecorder : public Variable;
```
# bvar::LatencyRecoder

A counter used for latency and qps. We can get latency / max_latency / qps / count as long as the latency data filled in. Time window is the last param, omit by `bvar_dump_interval`

LatencyRecoder is a compound variable, consisting of several bvar.
```c++
LatencyRecorder write_latency("table2_my_table_write");  // produces 4 variables:
                                                         //   table2_my_table_write_latency
                                                         //   table2_my_table_write_max_latency
                                                         //   table2_my_table_write_qps
                                                         //   table2_my_table_write_count
// In your write function
write_latency << the_latency_of_write;

  ```

# bvar::Window

Get data within a time window. Window cannot exist alone, it relies on a counter. Window will auto-update, we don't have to send data to it. For the sake of performance, the data comes from every-second sampling over the original counter, in the worst case, Window has one-second latency
```c++
// Get data within a time window.
// The time unit is 1 second fixed.
// Window relies on other bvar which should be constructed before this window and destructs after this window.
// R must:
// - have get_sampler() (not require thread-safe)
// - defined value_type and sampler_type
template <typename R>
class Window : public Variable;
```


# bvar::PerSecond

Get the mean value over the last amount of time. Its almost the same as Window, except for the value will be divided by the time window
```c++
bvar::Adder<int> sum;

// sum_per_second.get_value()is summing every-second value over the last 60 seconds, if we omit the time window, it's set to 'bvar_dump_interval' by default
bvar::PerSecond<bvar::Adder<int> > sum_per_second(&sum, 60);
```
**PerSecond does not always make sense**

There is no Maxer in the above code, since the max value over a period of time divided by the time window is meaningless.
```c++
bvar::Maxer<int> max_value;

// WRONG！max value divided by time window is pointless
bvar::PerSecond<bvar::Maxer<int> > max_value_per_second_wrong(&max_value);

// CORRECT. It's the right way to set the time window to 1s so that we can get the max value for every second
bvar::Window<bvar::Maxer<int> > max_value_per_second(&max_value, 1);
```


## Difference between Window and PerSecond

Suppose we want the memory change since last minute, if we use Window<>, the meaning for the returning value is "the memory increase over the last minute is 18M" if we use PerSecond<>, the meaning for return value will be "the average memory increase per second over the last minute is 0.3M”.

Pros of Window is preciseness, it fits in some small-number cases, like “the number of error produced over last minute“, if we use PerSecond, we might get something like "the average error rate per second over the last minute is 0.0167", which is very unclear as opposed to "one error produced over last minute". Some other non-time-related variables also fit in Window<>, such like calculating the CPU ratio over the last minute is using a Adder by summing CPU time and real time, then we use Window<> on top of the Adder to get the last-mintue CPU time and real time, dividing these two value then we get the CPU ratio for the last minute, which is not time-related. It will get wrong when use PerSeond



# bvar::Status

Record and display one value, has additional set_value() function
```c++
// Display a rarely or periodically updated value.
// Usage:
//   bvar::Status<int> foo_count1(17);
//   foo_count1.expose("my_value");
//
//   bvar::Status<int> foo_count2;
//   foo_count2.set_value(17);
//
//   bvar::Status<int> foo_count3("my_value", 17);
//
// Notice that Tp needs to be std::string or acceptable by boost::atomic<Tp>.
template <typename Tp>
class Status : public Variable;
```


# bvar::PassiveStatus

Display the value when needed. In some cases, we are not able to actively set_value nor set_value in a certain time interval. We'd better print it out when needed, user can pass in the print-out callback function to achieve this.
```c++
// Display a updated-by-need value. This is done by passing in an user callback
// which is called to produce the value.
// Example:
//   int print_number(void* arg) {
//      ...
//      return 5;
//   }
//
//   // number1 : 5
//   bvar::PassiveStatus status1("number1", print_number, arg);
//
//   // foo_number2 : 5
//   bvar::PassiveStatus status2(typeid(Foo), "number2", print_number, arg);
template <typename Tp>
class PassiveStatus : public Variable;

even though it looks simple, PassiveStatus is one of the most useful bvar, since most of the statistic values have already existed, we don't have to store it again, just fetch the data according to our need. Declare a bvar which can display user process name as below：

static void get_username(std::ostream& os, void*) {
    char buf[32];
    if (getlogin_r(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf)-1] = '\0';
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
//                                                ^
//                                            the gflag name

// Expose the gflag as a bvar named "foo_bar_my_flag_that_matters".
static bvar::GFlag s_gflag_my_flag_that_matters_with_prefix("foo_bar", "my_flag_that_matters");
```
