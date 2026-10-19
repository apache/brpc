- [mbvar Introduction](#mbvar-introduction)
- [bvar::MVariables](#bvarmvariables)
  - [expose](#expose)
    - [expose](#expose-1)
    - [expose_as](#expose_as)
  - [Export all MVariable](#export-all-mvariable)
  - [count](#count)
    - [count_exposed](#count_exposed)
  - [list](#list)
    - [list_exposed](#list_exposed)
- [bvar::MultiDimension](#bvarmultidimension)
  - [constructor](#constructor)
  - [stats](#stats)
    - [get_stats](#get_stats)
  - [count](#count-1)
    - [count_labels](#count_labels)
    - [count_stats](#count_stats)
  - [list](#list-1)
    - [list_stats](#list_stats)
  - [template](#template)
    - [bvar::Adder](#bvaradder)
    - [bvar::Maxer](#bvarmaxer)
    - [bvar::Miner](#bvarminer)
    - [bvar::IntRecorder](#bvarintrecorder)
    - [bvar::LatencyRecorder](#bvarlatencyrecorder)
    - [bvar::Histogram](#bvarhistogram)
    - [bvar::Status](#bvarstatus)
    - [bvar::WindowEx](#bvarwindowex)
    - [bvar::PerSecondEx](#bvarpersecondex)
    - [自定义的组合类型](#自定义的组合类型)

# mbvar Introduction

多维度mbvar使用文档

mbvar中有两个类，分别是MVariable和MultiDimension，MVariable是多维度统计的基类，MultiDimension是派生模板类，目前支持如下几种类型：

| bvar类型 | 说明 |
| ------ | ------ |
| bvar::Adder<T> | 计数器，默认0，varname << N相当于varname += N。 |
| bvar::Maxer<T> | 求最大值，默认std::numeric_limits<T>::min()，varname << N相当于varname = max(varname, N)。 |
| bvar::Miner<T> | 求最小值，默认std::numeric_limits<T>::max()，varname << N相当于varname = min(varname, N)。 |
| bvar::IntRecorder | 求自使用以来的平均值。注意这里的定语不是“一段时间内”。一般要通过Window衍生出时间窗口内的平均值。 |
| bvar::LatencyRecorder | 专用于记录延时和qps的变量。输入延时，平均延时/最大延时/qps/总次数 都有了。 |
| bvar::Histogram | 把观测值落到一组固定的桶里，只导出各个桶的计数，分位值由监控系统算。没有默认构造函数，创建MultiDimension时必须额外给出一个BucketSchema，见[bvar::Histogram](#bvarhistogram)一节。 |
| bvar::Status<T> | 记录和显示一个值，拥有额外的set_value函数。 |
| bvar::WindowEx<R, T> | 获得之前一段时间内的统计值。WindowEx是独立存在的，不依赖其他的计数器，需要给它发送数据。 |
| bvar::PerSecondEx<T> | 获得之前一段时间内平均每秒的统计值。PerSecondEx是独立存在的，不依赖其他的计数器，需要给它发送数据。 |

值类型不限于上表：凡是「一个类型对应多个prometheus指标」的类型，只要在该类型上声明`list_metric_families()`和`dump_samples()`两个成员，MultiDimension就会把它当作组合指标来导出，不需要修改bvar的任何文件，见[自定义的组合类型](#自定义的组合类型)一节。bvar::Histogram和bvar::LatencyRecorder走的正是这同一个约定。

例子：
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});
// bvar::MultiDimension<bvar::Adder<int> > g_request_count("foo_bar", "request_count", {"idc", "method", "status"});

int process_request(const std::list<std::string>& request_label) {
    // 获取request_label对应的单维度bvar指针，比如：request_label = {"tc", "get", "200"}
    bvar::Adder<int>* adder = g_request_count.get_stats(request_label);
    // 判断指针非空
    if (!adder) {
        return -1;
    }
    // adder只能在g_request_count的生命周期内访问，否则行为未定义，可能会出core
    // 给adder输入一些值
    *adder << 1 << 2 <<3;
    LOG(INFO) << "adder=" << *adder; // adder add up to 6
    ...
    return 0;
}

} // namespace bar
} // namespace foo
```

# bvar::MVariables
MVariale是MultiDimension(多维度统计)的基类，主要提供全局注册、列举、查询和dump等功能。

## expose

### expose

用户在创建mbvar变量(bvar::MultiDimension)的时候，如果使用一个参数的构造函数(共有三个构造函数)，这个mbvar并未注册到任何全局结构中(当然也不会dump到本地文件)，在这种情况下，mbvar纯粹是一个更快的多维度计数器。我们称把一个mbvar注册到全局表中的行为为“曝光”，可以通过expose函数曝光：

```c++
class MVariable {
public
    ...
    // Expose this mvariable globally so that it's counted in following
    // functions:
    //     list_exposed
    //     count_exposed
    // Return 0 on success, -1 otherwise.
    int expose(const base::StringPiece& name);
    int expose_as(const base::StringPiece& prefix, const base::StringPiece& name);
};
```

全局曝光后的mbvar名字便为name或者prefix+name，可通过以_exposed为后缀的static函数查询。比如MVariable::describe_exposed(name)会返回名为name的mbvar的描述。

当相同名字的mbvar已存在时，expose会打印FATAL日志并返回-1。如果选项-bvar_abort_on_same_name设为true (默认是false)，程序会直接abort。

下面是一些曝光mbvar的例子：
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

// 换一个名字
g_request_count->expose("request_count_another");

int process_request(const std::list<std::string>& request_label) {
    // 获取request_label对应的单维度bvar指针，比如：request_label = {"tc", "get", "200"}
    bvar::Adder<int>* adder = g_request_count.get_stats(labels_value);
    // 判断指针非空
    if (!adder) {
        return -1;
    }

    //adder只能在g_request_count的生命周期内访问，否则行为未定义，可能会出core
    *adder << 10 << 20 << 30; // adder add up to 60
}

} // namespace bar
} // namespace foo
```

### expose_as

为了避免重名，mbvar的名字应加上前缀，建议为\<namespace\>\_\<module\>\_\<name\>。为了方便使用，我们提供了expose_as函数，接收一个前缀。
```c++
class MVariable {
public
    ...
    // Expose this mvariable with a prefix.
    // Example:
    //   namespace foo {
    //   namespace bar {
    //
    //      bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});
    //      g_request_count.expose_as("foo_bar", "request_count");
    //      ...
    //
    //   }  // foo
    //   }  // bar
    int expose_as(const base::StringPiece& prefix, const base::StringPiece& name);
};
```

## Export all MVariable
提供dump_exposed函数导出进程中所有已曝光的mbvar：

```c++
// Implement this class to write mvariables into different places.
// If dump() returns false, MVariable::dump_exposed() skip and continue dump.
class Dumper {
public:
    virtual bool dump(const std::string& name, const base::StringPiece& description) = 0;
};

// Options for MVariable::dump_exposed().
struct DumpOptions {
    // Contructed with default options.
    DumpOptions();
    // If this is true, string-type values will be quoted.
    bool quote_string;
    // The ? in wildcards. Wildcards in URL need to use another character
    // because ? is reserved.
    char question_mark; // 目前不支持
    // Separator for white_wildcards and black_wildcards.
    char wildcard_separator; // 目前不支持
    // Name matched by these wildcards (or exact names) are kept.
    std::string white_wildcards; // 目前不支持
    // Name matched by these wildcards (or exact names) are skipped.
    std::string black_wildcards; // 目前不支持
};

class MVariable {
    ...
    ...
    // Find all exposed mvariables and send them to `dumper'.
    // Use default options when `options' is nullptr.
    // Return number of dumped mvariables, -1 on error.
    static size_t dump_exposed(Dumper* dumper, const DumpOptions* options);
};
```

最常见的导出需求是通过HTTP接口查询和写入本地文件，多维度统计支持通过内置服务`/brpc_metrics`接口方式查询。也支持写入本地文件，该功能由下面5个gflags控制，你的程序需要使用gflags。

| 名称 | 默认值 | 描述 |
| ------ | ------ | ------ |
| mbvar_dump |false	| Create a background thread dumping(shares the same thread as bvar_dump) all mbvar periodically, all bvar_dump_* flags are not effective when this flag is off |
| mbvar_dump_file | monitor/mbvar.\<app\>.data | Dump mbvar into this file |
| mbvar_dump_format	| common | Dump mbvar write format <br> common：文本格式，Key和Value用冒号分割(和目前的单维度dump文件格式一致) <br><br> prometheus：文本格式，Key和Value用空格分开protobuf：二进制格式，暂时不支持|
| bvar_dump_interval | 10 |Seconds between consecutive dump |
| mbvar_dump_prefix | \<app\> | Every dumped name starts with this prefix |
| bvar_max_dump_multi_dimension_metric_number | 0 | 最多导出的mbvar指标数，默认是0，即不导出任何mbvar。计数以指标为单位，一个label组合可能贡献多个指标（Histogram导出`_bucket`/`_sum`/`_count`，LatencyRecorder导出各个分位值） |

用户可在程序启动前加上对应的gflags。

>当mbvar_dump=true并且mbvar_dump_file不为空时，程序会启动一个后台导出线程以bvar_dump_interval指定的间隔更新mbvar_dump_file，其中包含所有的多维统计项mbvar。

比如我们把所有的gflags修改为下表：
| 名称 | 默认值 | 描述 |
| ------ | ------ | ------ |
| mbvar_dump |true	| Create a background thread dumping(shares the same thread as bvar_dump) all mbvar periodically, all bvar_dump_* flags are not effective when this flag is off |
| mbvar_dump_file | monitor/mbvar.\<app\>.data | Dump mbvar into this file |
| mbvar_dump_format	| common | Dump mbvar write format <br> common：文本格式，Key和Value用冒号分割(和目前的单维度dump文件格式一致) <br><br> prometheus：文本格式，Key和Value用空格分开protobuf：二进制格式，暂时不支持|
| bvar_dump_interval | 10 |Seconds between consecutive dump |
| mbvar_dump_prefix | mbvar | Every dumped name starts with this prefix |
| bvar_max_dump_multi_dimension_metric_number | 2000 | 最多导出的mbvar指标数，默认是0，即不导出任何mbvar。计数以指标为单位，一个label组合可能贡献多个指标（Histogram导出`_bucket`/`_sum`/`_count`，LatencyRecorder导出各个分位值） |

导出的本地文件为monitor/mbvar.\<app\>.data：
```
mbvar_test_concurrent_write_and_read{idc=idc3,method=method6,status=status40} : 76896
mbvar_test_concurrent_write_and_read{idc=idc5,method=method6,status=status36} : 147910
mbvar_test_concurrent_write_and_read{idc=idc5,method=method12,status=status23} : 209270
mbvar_test_concurrent_write_and_read{idc=idc3,method=method1,status=status6} : 116325
mbvar_test_concurrent_write_and_read{idc=idc5,method=method10,status=status29} : 193536
mbvar_test_concurrent_write_and_read{idc=idc5,method=method15,status=status6} : 294726
mbvar_test_concurrent_write_and_read{idc=idc3,method=method2,status=status13} : 136676
mbvar_test_concurrent_write_and_read{idc=idc2,method=method5,status=status49} : 43203
mbvar_test_concurrent_write_and_read{idc=idc5,method=method10,status=status1} : 312499
mbvar_test_concurrent_write_and_read{idc=idc1,method=method10,status=status35} : 35698
```

如果你的程序没有使用brpc，仍需要动态修改gflags（一般不需要），可以调用google::SetCommandLineOption()，如下所示：

```c++
#include <gflags/gflags.h>
...
if (google::SetCommandLineOption("mbvar_dump_format", "prometheus").empty()) {
    LOG(ERROR) << "Fail to set mbvar_dump_format";
    return -1;
}
LOG(INFO) << "Successfully set mbvar_dump_format to prometheus";
```

>请勿直接设置 FLAGS_mbvar_dump_file / FLAGS_mbvar_dump_format / FLAGS_bvar_dump_prefix，如果有需求可以调用google::SetCommandLineOption()方法进行修改。

一方面这些gflag类型都是std::string，直接覆盖是线程不安全的；另一方面不会触发validator（检查正确性的回调），所以也不会启动后台导出线程。


## count
统计相关函数
```c++
class MVariable {
public:
    ...
    // Get number of exposed mvariables
    static size_t count_exposed();
};
```

### count_exposed

获取目前已经曝光的多维度统计项mbvar的个数，注意：这里是多维度统计项(多维度mbvar变量)，而不是维度数。
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

// 定义另一个全局多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_psmi_count("psmi_count", {"product", "system", "module", "interface"});

size_t count_exposed() {
    size_t mbvar_count_exposed = bvar::MVariable::count_exposed();
    CHECK_EQ(2, mbvar_count_exposed);
    return mbvar_count_exposed;
}

} // namespace bar
} // namespace foo
```

使用说明
> 一般情况下用不到count_系列统计函数，如果有特殊需求，也不建议频繁调用。


## list
```c++
class MVariable {
public:
    ...
    // Put names of all exposed mbvariable into `names'
    static size_t list_exposed(std::vector<std::string>* names);
};
```

### list_exposed
获取所有曝光的多维度统计项mbvar名称
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

// 定义另一个全局多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_psmi_count("psmi_count", {"product", "system", "module", "interface"});

size_t mbvar_list_exposed(std::vector<std::string>* names) {
    if (!names) {
        return -1;
    }

    // clear
    names.clear();
    bvar::MVariable::list_exposed(names);
    // names：[ "request_count", "psmi_count" ]
    CHECK_EQ(2, names->size());
    return names->size();
}

} // namespace bar
} // namespace foo
```

# bvar::MultiDimension

多维度统计的实现，主要提供bvar的获取、列举等功能。

为了兼容旧的用法，KeyType默认类型是std::list<std::string>。KeyType必须是（STL或者自定义）容器，value_type必须是std::string。

## constructor

有三个构造函数，labels之后都可以再带任意个参数，它们会以`std::decay_t`后的类型拷贝进MultiDimension，并在创建每个label组合对应的bvar时，以const引用的形式转发给值类型T的构造函数：
```c++
template <typename T, typename KeyType = std::list<std::string>, bool Shared = false>
class MultiDimension : public MVariable {
public:
    // 不建议使用
    template <typename... Args>
    explicit MultiDimension(const key_type& labels, Args&&... args);

    // 推荐使用
    template <typename... Args>
    MultiDimension(const base::StringPiece& name,
                   const key_type& labels, Args&&... args);
    // 推荐使用
    template <typename... Args>
    MultiDimension(const base::StringPiece& prefix,
                   const base::StringPiece& name,
                   const key_type& labels, Args&&... args);
    ...
};
```

在编译期校验：
- 不传额外参数时，值类型必须支持默认构造；
- 传了参数时，值类型必须能用这些参数的const引用构造出来，否则编译期就报错。比如bvar::Histogram没有默认构造函数，创建MultiDimension<bvar::Histogram>时必须显式给出一个BucketSchema：

```c++
bvar::MultiDimension<bvar::Histogram> g_latency(
    "rpc_latency", {"method", "status"},
    bvar::Histogram::BucketSchema({10, 50, 100, 500, 1000}));
```

这个转发是通用的，不是Histogram的特例。比如`MultiDimension<bvar::LatencyRecorder> m("m", {"method"}, 60)`就是给每个LatencyRecorder指定60秒的统计窗口。

注意参数的生命周期：按`std::decay_t`保存意味着`const char*`、butil::StringPiece这类只借用内存的参数，存下来的是指针本身，而不是字符的拷贝。而每个label组合对应的bvar是惰性创建的，第一次get_stats该组合时才会真正读取这些参数。因此**调用方要保证被借用的内存活得足够久**，至少覆盖到最后一个label组合的创建，最省心的做法是让它活得和MultiDimension一样久。字符串字面量总是满足这一点；局部的std::string不满足，直接以std::string传入即可得到一份拷贝：

```c++
// OK：字面量是静态存储期的
bvar::MultiDimension<MyValue> m1("m1", {"method"}, "default_tag");

// OK：参数类型是std::string，值被拷贝进MultiDimension
std::string tag = build_tag();
bvar::MultiDimension<MyValue> m2("m2", {"method"}, tag);

// 错误：存的是tag.c_str()这个指针，tag析构后再创建bvar就是悬垂读
bvar::MultiDimension<MyValue> m3("m3", {"method"}, tag.c_str());
```

**explicit MultiDimension(const key_type& labels, Args&&... args)**

* ~~不建议使用~~
* 不会“曝光”多维度统计变量(mbvar)，即没有注册到任何全局结构* 中
* 不会dump到本地文件，即使-bvar_dump=true、-mbvar_dump_file不为空
* mbvar纯粹是一个更快的多维度计数器

**MultiDimension(const base::StringPiece& name, const key_type& labels, Args&&... args)**

* **推荐使用**
* 会曝光(调用MVariable::expose(name))，也会注册到全局结构中
* -bvar_dump=true时，会启动一个后台导出线程以bvar_dump_interval指定的时间间隔更新mbvar_dump_file文件

```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

} // namespace bar
} // namespace foo
```

**MultiDimension(const base::StringPiece& prefix, const base::StringPiece& name, const key_type& labels, Args&&... args)**
* **推荐使用**
* 会曝光(调用MVariable::expose_as(prefix, name))，也会注册到全局结构中
* -bvar_dump=true时，会启动一个后台导出线程以bvar_dump_interval指定的时间间隔更新mbvar_dump_file文件

```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("foo_bar", "request_count", {"idc", "method", "status"});

} // namespace bar
} // namespace foo
```

## stats
```c++
template <typename T, typename KeyType = std::list<std::string>>
class MultiDimension : public MVariable {
public:
    ...

    // Get real bvar pointer object
    // Return real bvar pointer(Not nullptr) on success, nullptr otherwise.
    T* get_stats(const std::list<std::string>& labels_value);
};
```

### get_stats

根据指定label获取对应的单维度统计项bvar。

get_stats除了支持（默认）std::list<std::string>参数类型，也支持自定义参数类型，满足以下条件：
1. （STL或者自定义）容器。
2. K::value_type支持通过operator std::string()转换为std::string和通过operator butil::StringPiece()转换为butil::StringPiece。
3. K::value_type支持和std::string进行比较。

推荐使用不需要分配内存的容器（例如，std::array、absl::InlinedVector）和不需要拷贝字符串的数据结构（例如，const char*、std::string_view、butil::StringPieces），可以提高性能。

bvar::MultiDimension的模板参数Shared，默认为false：
1. 如果Shared等于false，get_stats返回模板参数T的指针，例如bvar::Adder<int>*。
2. 如果Shared等于true，get_stats返回模板参数T的shared_ptr，例如std::shared_ptr\<bvar::Adder<int>\>。

**注意**：因为shared_ptr的开销，Shared等于true的性能会比Shared等于false的性能差一些。

```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

template <typename K>
int get_request_count(const K& request_label) {
    // 获取request_label对应的单维度bvar指针，比如：request_label = {"tc", "get", "200"}
    bvar::Adder<int> *request_adder = g_request_count.get_stats(request_label);
    // 判断指针非空
    if (!request_adder) {
        return -1;
    }

    // request_adder只能在g_request_count的生命周期内访问，否则行为未定义，可能会出core
    *request_adder << 1;
    return request_adder->get_value();
}

std::list<std::string> request_label_list = {"tc", "get", "200"};
int request_count = get_request_count(request_label_list);

std::vector<std::string_view> request_label_list = {"tc", "get", "200"};
int request_count = get_request_count(request_label_list);

std::vector<butil::StringPiece> request_label_list = {"tc", "get", "200"};
int request_count = get_request_count(request_label_list);

class MyStringView {
public:
    MyStringView() : _ptr(nullptr), _len(0) {}
    MyStringView(const char* str)
        : _ptr(str),
          _len(str == nullptr ? 0 : strlen(str)) {}
#if __cplusplus >= 201703L
    MyStringView(const std::string_view& str)
        : _ptr(str.data()), _len(str.size()) {}
#endif // __cplusplus >= 201703L
    MyStringView(const std::string& str)
        : _ptr(str.data()), _len(str.size()) {}
    MyStringView(const char* offset, size_t len)
        : _ptr(offset), _len(len) {}

    const char* data() const { return _ptr; }
    size_t size() const { return _len; }

    // Converts to `std::basic_string`.
    explicit operator std::string() const {
        if (nullptr == _ptr) {
            return {};
        }
        return {_ptr, size()};
    }

    // Converts to butil::StringPiece.
    explicit operator butil::StringPiece() const {
        if (nullptr == _ptr) {
            return {};
        }
        return {_ptr, size()};
    }

private:
    const char* _ptr;
    size_t _len;
};

bool operator==(const MyStringView& x, const std::string& y) {
    if (x.size() != y.size()) {
        return false;
    }
    return butil::StringPiece::wordmemcmp(x.data(), y.data(), x.size()) == 0;
}
bool operator==(const std::string& x, const MyStringView& y) {
    if (x.size() != y.size()) {
        return false;
    }
    return butil::StringPiece::wordmemcmp(x.data(), y.data(), x.size()) == 0;
}

std::vector<MyStringView> request_label_list = {"tc", "get", "200"};
int request_count = get_request_count(request_label_list);

} // namespace bar
} // namespace foo
```

**内部实现逻辑**

判断该label是否已经存在对应的单维度统计项bvar：

* 存在
    * return bvar
* 不存在
    * new bvar()
    * store(bvar)
    * return bvar

### delete_stats

根据指定label删除对应的单维度统计项bvar。

bvar::MultiDimension的模板参数Shared，默认为false：
1. 如果Shared等于false，get_stats返回的是模板参数T的指针，delete_stats无法保证没有使用者，所以无法安全删除bvar。
2. 如果Shared等于true，get_stats返回的是模板参数T的shared_ptr，delete_stats可以安全删除bvar。

### clear_stats

清理所有单维度统计项bvar。

安全性同样受bvar::MultiDimension的模板参数Shared的影响，见delete_stats一节说明。

**bvar的生命周期**

label对应的单维度统计项bvar存储在多维度统计项(mbvar)中，当mbvar析构的时候会释放自身所有bvar，所以用户必须保证在mbvar的生命周期之内操作bvar，在mbvar生命周期外访问bvar的行为未定义，极有可能出core。

## count
```c++
class MVariable {
public:
    ...

    // Get number of mvariable labels
    size_t count_labels() const;
};

template <typename T, typename KeyType = std::list<std::string>>
class MultiDimension : public MVariable {
public:
    ...

    // Get number of stats
    size_t count_stats();
};
```

### count_labels

获取多维度统计项的labels个数，用户在创建多维度(bvar::MultiDimension)统计项的时候，需要提供类型为std::list\<std::string\>的labels变量，我们提供了count_labels函数，返回labels的长度。

```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

size_t count_labels() {
    size_t mbvar_count_labels = g_request_count.count_labels();
    CHECK_EQ(3, mbvar_count_labels);
    return mbvar_count_labels;
}
} // namespace bar
} // namespace foo
```

### count_stats

获取多维度(bvar::MultiDimension)统计项的维度(stats)数
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

size_t count_stats() {
    // 获取request1对应的单维度mbvar指针，假设request1_labels = {"tc", "get", "200"}
    bvar::Adder<int> *request1_adder = g_request_count.get_stats(request1_labels);
    // 判断指针非空
    if (!request1_adder) {
        return -1;
    }
    // request1_adder只能在g_request_count生命周期内访问，否则行为未定义，可能会出core
    *request1_adder << 1;

    // 获取request2对应的单维度mbvar指针，假设request2_labels = {"nj", "get", "200"}
    bvar::Adder<int> *request2_adder = g_request_count.get_stats(request2_labels);
    // 判断指针非空
    if (!request2_adder) {
        return -1;
    }
    // request2_adder只能在g_request_count生命周期内访问，否则行为未定义，可能会出core
    *request2_adder << 1;


    size_t mbvar_count_stats = g_request_count.count_stats();
    CHECK_EQ(2, mbvar_count_stats);
    return mbvar_count_stats;
}
} // namespace bar
} // namespace foo
```

## list
```c++
template <typename T, typename KeyType = std::list<std::string>>
class MultiDimension : public MVariable {
public:
    ...
    // Put all stats labels into `names'
    void list_stats(std::vector<std::list<std::string> >* names);
};
```

### list_stats
获取一个多维度统计项下所有labels组合列表

```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_count("request_count", {"idc", "method", "status"});

size_t list_stats(std::vector<std::list<std::string> > *stats_names) {
    if (!stats_names) {
        return -1;
    }

    // clear
    stats_names.clear();

    // 获取request1对应的单维度mbvar指针，假设request1_labels = {"tc", "get", "200"}
    bvar::Adder<int> *request1_adder = g_request_count.get_stats(request1_labels);
    // 判断指针非空
    if (!request1_adder) {
        return -1;
    }

    // 获取request2对应的单维度mbvar指针，假设request2_labels = {"nj", "get", "200"}
    bvar::Adder<int> *request2_adder = g_request_count.get_stats(request2_labels);
    // 判断指针非空
    if (!request2_adder) {
        return -1;
    }

    g_request_count.list_stats(stats_names);
    // labels_names：
    // [
    //      {"tc", "get", "200"},
    //      {"nj", "get", "200"}
    // ]

    CHECK_EQ(2, stats_names.size());
    return stats_names.size();
}

} // namespace bar
} // namespace foo
```

**使用说明**

一般情况下用户不需要获取labels组合列表，如果有特殊需求，也不建议频繁调用，否则可能影响get_stats的写入性能。

## template

### bvar::Adder
顾名思义，用于累加
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Adder<int> > g_request_cost("request_count", {"idc", "method", "status"});

int request_cost_total(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::Adder<int>* cost_add = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_add) {
        return -1;
    }
    // cost_add只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    *cost_add << 1 << 2 << 3 << 4;
    CHECK_EQ(10, cost_add->get_value());
    return cost_add->get_value();
}

} // namespace bar
} // namespace foo
```

### bvar::Maxer
用于取最大值，运算符为std::max
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Maxer<int> > g_request_cost("request_cost", {"idc", "method", "status"});

int request_cost_max(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::Maxer<int>* cost_max = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_max) {
        return -1;
    }

    // cost_max只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    *cost_max << 1 << 2 << 3 << 4;
    CHECK_EQ(4, cost_max->get_value());
    return cost_max->get_value();
}

} // namespace bar
} // namespace foo
```

### bvar::Miner
用于取最小值，运算符为std::min
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Miner<int> > g_request_cost("request_cost", {"idc", "method", "status"});

int request_cost_min(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::Miner<int>* cost_min = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_min) {
        return -1;
    }

    // cost_min只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    *cost_min << 1 << 2 << 3 << 4;
    CHECK_EQ(1, cost_min->get_value());
    return cost_min->get_value();
}

} // namespace bar
} // namespace foo
```

### bvar::IntRecorder
用于计算平均值。
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::IntRecorder> g_request_cost("request_cost", {"idc", "method", "status"});

int request_cost_avg(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::IntRecorder* cost_avg = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_avg) {
        return -1;
    }

    // cost_avg只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    *cost_avg << 1 << 3 << 5;
    CHECK_EQ(3, cost_avg->get_value());
    return cost_avg->get_value();
}

} // namespace bar
} // namespace foo
```

### bvar::LatencyRecorder
专用于计算latency和qps的计数器。只需填入latency数据，就能获取latency / max_latency / qps / count，统计窗口是bvar_dump_interval。
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::LatencyRecorder> g_request_cost("request_cost", {"idc", "method", "status"});

void request_cost_latency(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::LatencyRecorder* cost_latency = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_latency) {
        return -1;
    }

    // cost_latency只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    *cost_latency << 1 << 2 << 3 << 4 << 5 << 6 << 7;

    // 获取latency
    int64_t request_cost_latency = cost_latency->latency();
    // 获取max_latency
    int64_t request_cost_max_latency = cost_latency->max_latency();
    // 获取qps
    int64_t request_cost_qps = cost_latency->qps();
    // 获取count
    int64_t request_cost_count = cost_latency->count();
}

} // namespace bar
} // namespace foo
```

### bvar::Histogram
把观测值落到一组固定的桶里，只导出各个桶的计数，分位值由监控系统用`histogram_quantile()`去算，适合跨实例聚合的场景。

Histogram没有默认构造函数，创建MultiDimension时必须在labels之后显式给出一个BucketSchema，它会转发给每个label组合创建的Histogram：
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量，桶的上界为10/20/50/100/500，另有一个+Inf桶
bvar::MultiDimension<bvar::Histogram> g_request_latency(
    "request_latency", {"idc", "method", "status"},
    bvar::Histogram::BucketSchema({10, 20, 50, 100, 500}));

void record_latency(const std::list<std::string>& request_labels, int64_t latency_us) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::Histogram* latency = g_request_latency.get_stats(request_labels);
    // 判断指针非空
    if (latency == nullptr) {
        return;
    }

    // latency只能在g_request_latency生命周期内访问，否则行为未定义，可能会出core
    *latency << latency_us;
    // 获取自使用以来的样本数、总和与平均值
    int64_t count = latency->count();
    double sum = latency->sum();
    double avg = latency->average();
}

} // namespace bar
} // namespace foo
```

导出为prometheus格式时，mbvar的label和桶的`le`合并在同一个花括号里，`_bucket`/`_sum`/`_count`同属一个指标名：
```
# HELP request_latency
# TYPE request_latency histogram
request_latency_bucket{idc="tc",method="get",status="200",le="10"} 3
...
request_latency_bucket{idc="tc",method="get",status="200",le="+Inf"} 12
request_latency_sum{idc="tc",method="get",status="200"} 218
request_latency_count{idc="tc",method="get",status="200"} 12
```

### bvar::Status
记录和显示一个值，拥有额外的set_value函数。
```c++
#include <bvar/bvar.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::Status<int> > g_request_cost("request_cost", {"idc", "method", "status"});

void request_cost(const std::list<std::string>& request_labels) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::Status<int>* cost_status = g_request_cost.get_stats(request_labels);
    // 判断指针非空
    if (!cost_status) {
        return -1;
    }

    // cost_status只能在g_request_cost生命周期内访问，否则行为未定义，可能会出core
    cost_status->set_value(5);
    CHECK_EQ(5, cost_status->get_value());
}

} // namespace bar
} // namespace foo
```

### bvar::WindowEx
获得之前一段时间内的统计值。
```c++
#include <bvar/bvar.h>
#include <bvar/window.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::WindowEx<bvar::Adder<int>, 60>> sum_minute("sum_minute", {"idc", "method", "status"});

void Record(const std::list<std::string>& request_labels, int num) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::WindowEx<bvar::Adder<int>, 60>* status = sum_minute.get_stats(request_labels);
    // status只能在sum_minute生命周期内访问，否则行为未定义，可能会出core
    *status << num;
}

} // namespace bar
} // namespace foo
```

### bvar::PerSecondEx
获得之前一段时间内平均每秒的统计值。
```c++
#include <bvar/bvar.h>
#include <bvar/window.h>
#include <bvar/multi_dimension.h>

namespace foo {
namespace bar {
// 定义一个全局的多维度mbvar变量
bvar::MultiDimension<bvar::PerSecondEx<bvar::Adder<int>>> sum_per_second("sum_per_second", {"idc", "method", "status"});

void Record(const std::list<std::string>& request_labels, int num) {
    // 获取request对应的单维度mbvar指针，假设request_labels = {"tc", "get", "200"}
    bvar::PerSecondEx<bvar::Adder<int>>* status = sum_per_second.get_stats(request_labels);
    // status只能在sum_per_second生命周期内访问，否则行为未定义，可能会出core
    *status << num;
}

} // namespace bar
} // namespace foo
```

### 自定义的组合类型

上面几种类型都是一个类型对应一个prometheus指标。bvar::Histogram（一族`_bucket`/`_sum`/`_count`）和bvar::LatencyRecorder（`_latency`/`_avg_latency`等5个族）则会从一个类型导出多个指标。MultiDimension并没有为它们写死分支，而是认一个约定：在类型上声明下面两个成员，MultiDimension就会把它当作组合指标来dump。

```c++
class HitRate {
public:
    void hit() {
        ++_hits; 
        ++_total;
    }
    void miss() {
        ++_total;
    }

    // 本类型导出的族，按dump的顺序排列。一个MetricFamily就是一个prometheus
    // 指标族：一个指标名加一行`# TYPE`。它的三个字段依次是suffix（接在
    // 暴露名后面得到族名）、type（即`# TYPE`的值）、reserved_labels（该族
    // 样本自己的label名）。reserved_labels用不到时也要写上空初始化器`{}`，
    // 否则gcc会报-Wmissing-field-initializers告警。下面声明两个单样本的
    // counter族，暴露名cache_hitrate加上suffix后产出cache_hitrate_hits和
    // cache_hitrate_total。
    static std::vector<bvar::MetricFamily> list_metric_families() {
        return {{"_hits", "counter", {}}, {"_total", "counter", {}}};
    }

    // 只输出第family_index族的样本，不要输出"# TYPE"：那一行属于整个族，
    // 由调用方为所有label组合统一发一次。name是已经拼好suffix的暴露名；
    // labels是外层MultiDimension的label，形如`k="v",k="v"`，不带花括号。
    // 返回false表示中止，语义同Dumper::dump()。
    bool dump_samples(bvar::Dumper* dumper, size_t family_index,
                      const std::string& name,
                      butil::StringPiece labels) const {
        std::string key(name);
        if (!labels.empty()) {
            key.push_back('{');
            key.append(labels.data(), labels.size());
            key.push_back('}');
        }
        return dumper->dump_mvar(
            key, butil::Int64ToString(family_index == 0 ? _hits : _total));
    }

private:
    int64_t _hits{0};
    int64_t _total{0};
};

// 用法和内置类型完全一样
bvar::MultiDimension<HitRate> g_hitrate("cache_hitrate", {"cache"});
g_hitrate.get_stats({"l1"})->hit();
g_hitrate.get_stats({"l1"})->miss();
```

导出时每个族前面带一行自己的`# TYPE`，按list_metric_families()声明的顺序输出：
```
# HELP cache_hitrate_hits
# TYPE cache_hitrate_hits counter
cache_hitrate_hits{cache="l1"} 1
# HELP cache_hitrate_total
# TYPE cache_hitrate_total counter
cache_hitrate_total{cache="l1"} 2
```

几点约定：
* `list_metric_families()`必须是幂等的：它在每次dump时都会被调用，应保证每次返回相同的内容。返回的每个`bvar::MetricFamily`描述一个prometheus指标族（一个指标名加一行`# TYPE`，可包含一个或多个样本），按字段声明顺序包含三个字段（聚合初始化，用不到的字段也要写上空初始化器`{}`占位）：
  - `suffix`：族名后缀，拼在指标名后面构成族名。当一个类型导出多个平级的族时，每个族各用一个后缀，例如LatencyRecorder拆分为`_latency`/`_avg_latency`/`_max_latency`/`_qps`/`_count`五个族。Histogram则不同：整个类型只有一个族，`_bucket`/`_sum`/`_count`是同一族内的三类指标，而非三个独立的族，因此suffix留空。
  - `type`：`# TYPE`行的取值，为"gauge"/"counter"/"histogram"/"summary"之一。族内有哪些固定后缀的样本由type决定，这是prometheus文本格式的规定：summary有`_sum`/`_count`，histogram有`_bucket`/`_sum`/`_count`。
  - `reserved_labels`：该族内置的label。外层MultiDimension的labels不能与内置label冲突（Histogram为`le`，LatencyRecorder为`quantile`），冲突的MultiDimension无法曝光，原因见下一条。
* 外层MultiDimension的labels不能与某个族通过`reserved_labels`声明的label名重复（Histogram保留`le`，LatencyRecorder保留`quantile`），否则同一样本会包含重名的label。遇到冲突时MultiDimension只拒绝曝光：构造时打印一条ERROR日志，此后expose()/expose_as()一律返回-1，name()为空，也不会被dump出去。记录功能不受影响，get_stats()照常返回可用的bvar，不会变成nullptr。把外层的label改个名字，曝光即恢复正常。
* 同一样本内，外层MultiDimension的label要拼在自己的label之前，放进同一对花括号中。

