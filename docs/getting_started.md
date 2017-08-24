# 运行示例程序

在命令行中运行如下命令即可在~/my_baidu_rpc/public/baidu-rpc中下载源代码编译并运行echo示例程序：

> mkdir -p ~/my_baidu_rpc/public && cd ~/my_baidu_rpc/public && svn co
> https://svn.baidu.com/public/trunk/baidu-rpc && cd baidu-rpc && comake2 -UB -J8 -j8 && comake2 -P
> && make -sj8 && cd example/echo_c++ && comake2 -UB -J8 -j8 && comake2 -P && make -sj8 && (
> ./echo_server & ) && ./echo_client && pkill echo_server

# 通过COMAKE依赖baidu-rpc

如果你的模块还没有建立，你可能得去[work.baidu.com](http://work.baidu.com/)上申请新的模块。确保你使用了[comake2](http://wiki.babel.baidu.com/twiki/bin/view/Com/Main/Comake2)。

在COMAKE文件中增加：

> CONFIGS('public/baidu-rpc@ci-base')

 这依赖了baidu-rpc的最新发布版本。模板可参考[echo的COMAKE文件](https://svn.baidu.com/public/trunk/baidu-rpc/example/echo_c++/COMAKE)。然后运行：

```
$ comake2 -UB            # 下载所有的依赖模块
$ comake2 -P             # 生成或更新Makefile
$ make -sj8              # 编译
```

你也可以在[agile上](http://agile.baidu.com/#/builds/public/baidu-rpc@trunk)
或[scm.baidu.com](http://scm.baidu.com/)上查询baidu-rpc的已发布版本，并在COMAKE中依赖对应的静态版本。当comake2
-UB时baidu-rpc的版本会固定在你选定的版本上。注意：静态版本指的是我们发布的tag，而不是trunk上的revision。请勿依赖trunk的某个revision。

### gcc4下的errno问题

务必在直接或间接使用baidu-rpc的C/C++项目COMAKE中添加**CPPFLAGS('-D__const__=')**，防止[gcc4下的errno问题](http://wiki.baidu.com/display/RPC/thread-local#thread-local-gcc4下的errno问题)。

## comake出错

老版本comake2（比如2.1.3.2304）不支持ci-base版本，如果comake2报如下的错误，说明版本太老：

```
... [status:1][err:https://svn.baidu.com/public/tags/xxx/ci-base:  (Not a valid URL)
```

你可以运行如下命令更新comake2版本后再试，确保你有修改的权限。

```
$ cd $(dirname $(readlink -f $(which comake2)))/libcomake2
$ ./auto_update.py
```

## 更新依赖后编译失败

一般是baidu-rpc和依赖的模块版本不匹配导致的。确保以下模块没有被指定版本，让baidu-rpc自己选择：

[public/common](http://agile.baidu.com/agile/pipeline#/builds/public/common@trunk)

[public/bthread](http://agile.baidu.com/agile/pipeline#/builds/public/bthread@trunk)

[public/bvar](http://agile.baidu.com/agile/pipeline#/builds/public/bvar@trunk)

[public/murmurhash](http://agile.baidu.com/agile/pipeline#/builds/public/murmurhash@trunk)

[public/mcpack2pb](http://agile.baidu.com/agile/pipeline#/builds/public/mcpack2pb@trunk)

[public/iobuf](http://agile.baidu.com/agile/pipeline#/builds/public/iobuf@trunk)

[public/protobuf-json](http://agile.baidu.com/agile/pipeline#/builds/public/protobuf-json@trunk)

问题的根源在于百度的代码库不是单根的，所以每个模块都有很多独立版本, 产生非常多的组合,
有点类似于以前windows上的dll hell问题, 以baidu-rpc依赖的public/common为例:

- 情况1: 产品线依赖了public/common@某tag 和 public/baidu-rpc@ci-base, 所以baidu-rpc总是会更新到最新,
  而public/common则不会, 如果baidu-rpc依赖了public/common的新接口, baidu-rpc的编译就挂了.
- 情况2: 产品线依赖了public/common@ci-base 和 public/baidu-rpc@某tag,
  所以public/common总是会更新到最新, 但baidu-rpc不变, 如果public/common中删除了老接口,
或一些接口有调整, baidu-rpc的编译也挂了

如果去掉了public/common的依赖,
每次更新时COMAKE或BCLOUD会自动选择baidu-rpc对应版本被发布时使用的public/common版本,
比如baidu-rpc@某tag会选择该tag发布时对应的public/common版本, 一般总是可编译过的.

当然COMAKE或BCLOUD可能会对依赖打平, 如果产品线依赖的其他模块里依赖了public/common某版本且无法更改,
同时有编译问题, 只是在自己模块中去掉对public/common的依赖是不够的,
因为COMAKE或BCLOUD还是会选到那个指定的版本,
这种情况下可以在自己的COMAKE或BCLOUD中显式地依赖baidu-rpc@ci-base和public/common@ci-base.
基本原则就是都依赖ci-base一般总是对的, 不用过于担心稳定性问题. 

# 支持的软件栈

## GCC

| 版本   | 支持程度        |
| ---- | ----------- |
| 3.4  | 一直支持        |
| 4.4  | 15年9月后的版本支持 |
| 4.8  | 一直支持        |
| 5.4  | r34255后支持   |
| 7.1  | r35109后支持   |

使用其他版本的gcc可能会有warning而编译失败 (baidu-rpc把warning也视作error), 请联系我们修复。

r32023后支持C++11编译。

## GDB

如果你使用的是opt/compiler下的gcc(4.8)，那么gdb应该也应该用opt/compiler下的。

如果gdb对多线程的支持有问题，可以用这个版本，在目标机器上运行如下命令可获得gdb79可执行文件：

`wget "http://hetu.baidu.com:80/api/tool/getFile?toolId=1357&fileId=1201" -O "gdb79" && chmod +x
./gdb79`

## OS

RHEL4，centos 6.5，centos 4.3，ubuntu 14.04 (linuxmint 17.3)

ubuntu (linuxmint)下找不到openssl/ssl.h的话请安装libssl-dev。

对于自己的机器，安装好svn和[comake2](http://wiki.babel.baidu.com/twiki/bin/view/Com/Main/Comake2#安装升级)（公司环境中才能运行auto_udpate.py）后就可以按照第一节的方法下载和运行baidu-rpc了。

## protobuf

一直支持2.4。公司内请使用[2.4.1.1100](http://scm.baidu.com/fourversion/index.action?threeversion.id=346350&fourversion.id=668155)，这个版本中为google::protobuf::NewCallback增加了重载形式，可以支持超过2个参数。

r31361后支持2.6。公司内请使用[2.6.1.400](http://scm.baidu.com/fourversion/index.action?threeversion.id=644886&fourversion.id=1139940)，这个版本中为google::protobuf::NewCallback增加了重载形式，可以支持超过2个参数。

r32035后支持3.1.x，server端的arena分配暂不支持。不要求开启C++11，但要求gcc 4.8。

r35000后支持3.2.x。

### 更换protobuf版本

baidu-rpc默认的依赖2.4，你可以在你项目的COMAKE或BCLOUD中指定不同版本的protobuf，依赖会被打平。如果baidu-rpc依赖的一些模块报告protobuf版本不匹配，可以去对应的模块clean后重编。

### 关于NewCallback

由于protobuf
3把NewCallback设置为私有，r32035后baidu-rpc把NewCallback独立于[src/baidu/rpc/callback.h](https://svn.baidu.com/public/trunk/baidu-rpc/src/baidu/rpc/callback.h)，如果你的程序出现NewCallback相关的编译错误（不论protobuf的版本），把google::protobuf::NewCallback替换为baidu::rpc::NewCallback就行了。

### 编译.proto

COMAKE[支持proto文件](http://wiki.babel.baidu.com/twiki/bin/view/Com/Main/Comake2#如何编译idl或proto文件)作为源文件，但需要通过**PROTOC**指明protobuf
compiler的位置，在COMAKE文件中加入：

**COMAKE**

你也可以使用用如下命令手动生成代码，详见[公司内的pb文档](http://wiki.babel.baidu.com/twiki/bin/view/Com/Main/Protobuf)：

```
$ protoc --cpp_out=DEST_PATH -I=PROTO_PATH your.proto
```

更多protobuf问题请先阅读[google官方文档](https://developers.google.com/protocol-buffers/docs/reference/overview)，再阅读[公司内文档](http://wiki.babel.baidu.com/twiki/bin/view/Com/Main/Protobuf)，确保你已经了解protobuf的概念和基本使用方式。不确定的接口可以查看生成的.pb.h文件。

### 同时兼容pb 3.0和pb 2.x

勿使用proto3的新类型，proto文件开头要加上syntax="proto2";，[tools/add_syntax_equal_proto2_to_all.sh](https://svn.baidu.com/public/trunk/baidu-rpc/tools/add_syntax_equal_proto2_to_all.sh)可以给目录以下的所有没有加的proto文件加上syntax="proto2"。

## boost

要求1.56以上。r34354后开启C++11时不依赖boost。

**intrusive_ptr**

r34233后改为依赖public/common中的base::intrusive_ptr，不再依赖boost中的。

**context**

r34213后改为依赖public/bthread中的context，不再依赖boost中的。

r34213前如果在编译过程中出现相关的错误，可以尝试在COMAKE中显式地声明依赖：

**COMAKE**

**atomic**

r34354后改为依赖public/common中的base::atomic，不再（直接）依赖boost中的。当开启C++11时，base::atomic基于std::atomic，否则仍基于boost::atomic。换句话说，此版本以后的baidu-rpc当开启C++11时不再依赖boost。

## tcmalloc

baidu-rpc默认**不链接**[tcmalloc](http://goog-perftools.sourceforge.net/doc/tcmalloc.html)，如果需要可自行依赖，在COMAKE中增加：

**COMAKE**

tcmalloc相比默认的ptmalloc常可提升整体性能，建议尝试。但不同的tcmalloc版本可能有巨大的性能差异。tcmalloc
2.1.0.100会使baidu-rpc示例程序的性能显著地低于使用tcmalloc 1.7.0.200和2.5.0.5977的版本。甚至使用
1.7.0.100的性能也比1.7.0.200低一些，当你的程序出现性能问题时，去掉tcmalloc或更换版本看看。

使用gcc4.8.2编译的模块, 如果依赖的是third-64/tcmalloc(gcc3.4.5编译的产出),
可能会遇到main函数前malloc线程不安全的情况，具体表现为程序在main函数之前crash或者死锁. 例如

![img](http://wiki.baidu.com/download/attachments/71337200/image2017-8-22%2017%3A32%3A28.png?version=1&modificationDate=1503394348000&api=v2)

如果你的程序在gcc4.8编译的产出中遇到这个问题，建议升级tcmalloc为源码编译.

tcmalloc的另一个常见问题是它不像默认的ptmalloc那样及时的归还系统内存，所以在出现非法内存访问时，可能不会立刻crash，而最终crash在不相关的地方，甚至不crash。当你的程序出现诡异的内存问题时，也记得去掉tcmalloc看看。

如果要使用[cpu profiler](http://wiki.baidu.com/display/RPC/cpu+profiler)或[heap
profiler](http://wiki.baidu.com/display/RPC/heap+profiler)，请链接tcmalloc，这两个profiler是基于tcmalloc开发的。[contention
profiler](http://wiki.baidu.com/display/RPC/contention+profiler)不要求tcmalloc。

不想链接tcmalloc时请注意：不仅要去掉对tcmalloc模块的依赖，还得检查下是否删除了-DBAIDU_RPC_ENABLE_CPU_PROFILER
或 -DBAIDU_RPC_ENABLE_HEAP_PROFILER等baidu-rpc的宏。

## gflags

经验证支持2.0至[2.21](https://github.com/gflags/gflags/tree/v2.2.1)

## valgrind

在r30539后，在valgrind中运行程序时加上**-has_valgrind**
(确保你的程序使用了[gflags](http://wiki.baidu.com/pages/viewpage.action?pageId=71698818))

> 例如valgrind ./my_program
> -has_valgrind。如果没有开启的话，valgrind无法识别bthread的栈从而可能crash。
>
> 老版本的valgrind（比如3.2）似乎不支持，请使用新一点的版本（比如3.8），可以用[jumbo](http://jumbo.baidu.com/)安装。

在r34944后，-has_valgrind被移除，程序会自动判断是否在valgrind中。

## openssl

r35109后支持1.1

# 新特性

对用户有意义的新特性，以方便用户调研使用。

| 版本        | 功能                          | 描述                                       |
| --------- | --------------------------- | ---------------------------------------- |
| `r33446 ` | 开启-usercode_in_pthread无死锁风险 | 之前有。                                     |
| `r33424 ` | 增加开关-log_hostname           |
开启后会在每条日志后加上本机名。对于汇总的日志查询工具有用。           |
| `r33323 ` | 默认发布工具                      | 编译baidu-rpc时rpc_press, rpc_view, rpc_replay,
parallel_http也会一并编译，并能在产品库中获得。要注意的是，产品库默认以gcc
3.4编译，在新机器上可能无法直接运行，需要对一些so做软链。 |
| `r33306 ` | 增加工具parallel_http           | 可同时访问数万个http url,
远快于curl（即使批量后台运行）      |
| `r32844 ` | 支持http-flv                  | 另一种广泛用于直播的流媒体协议
|
| `r32803 ` | 支持同时发起大量异步访问                |
重构了bthread_id_list_t，从静态容量变为了动态容量。       |
| `r32668 ` | 支持RTMP                      | 一种广泛用于直播的流媒体协议（仍在完善中）
|
| `r32560 ` | 支持NamingServiceFilter       | 用于过滤名字服务返回的节点列表
|
| `r32536 ` | 初始化bthread                  |
`ServerOptions增加了bthread_init_fn等参数用于在server启动前初始化一些bthread。` |
| `r32420 ` | 支持nshead_mcpack             | 可用protobuf处理nshead+mcpack的协议             |
| `r32401 ` | 受控的日志打印                     | `LOG_ONCE:只打印一次　LOG_EVERY_N:每过N次打印一次
LOG_EVERY_SECOND:每秒打印一次` |
| `r32399 ` | 不可修改的flags                  | 加上-immutable_flags程序的/flags页面就无法被修改了
|
| `r32328 ` | 获取RPC延时                     |
Controller.latency_us()会返回对应的RPC延时，同步异步都支持。 |
| `r32301 ` | 显示RTT                       |
[/connections](http://brpc.baidu.com:8765/connections)页面会显示内核统计的smooth RTT了。 |
| `r32279 ` | 支持凤巢ITP协议                   |
详见[ITP](http://wiki.baidu.com/pages/viewpage.action?pageId=184259578) |
| `r32097`  | 支持Restful开发                 |
用户可定制访问每个方法的URL，详见[RestfulURL](http://wiki.baidu.com/pages/viewpage.action?pageId=213828736#id-实现HTTPService-RestfulURL)
|
| `r32034 ` | 支持protobuf 3.0              | `Server端的Arena分配仍不支持。mcpack2pb,
protobuf-json等周边工具仍待迁移。` |
| `r32015 ` | 访问redis-server              |
[访问Redis](http://wiki.baidu.com/pages/viewpage.action?pageId=213828705) |
| `r32009`  | RetryPolicy                 |
可定制重试策略，详见[重试](http://wiki.baidu.com/pages/viewpage.action?pageId=213828685#id-创建和访问Client-错误值得重试)
|
| `r32009 ` | rpc_view                    |
可在浏览器中查看端口不在[8000-8999]的内置服务，详见[rpc_view](http://wiki.baidu.com/pages/viewpage.action?pageId=167651918)
|
| `r31986 ` | rpc_press                   |
代替了pbrpcpress，详见[rpc_press](http://wiki.baidu.com/pages/viewpage.action?pageId=97645422) |
| `r31901 ` | contention profiler         | 可分析在锁上的等待时间，详见[contention
profiler](http://wiki.baidu.com/pages/viewpage.action?pageId=165876314) |
| `r31658 ` | rpc dump & replay           |
详见[rpc_replay](http://wiki.baidu.com/pages/viewpage.action?pageId=158707916) |

# FAQ

### Q: baidu-rpc会不会发布稳定版本

本项目是主干开发，最新的改动在[trunk](https://svn.baidu.com/public/trunk/baidu-rpc/)，发布在[agile上](http://agile.baidu.com/#/builds/public/baidu-rpc@trunk)。我们会尽量保持已有接口不变，升级新版本一般不会break代码。由于开发节奏快，我们没有发布Releasing
Branch (RB)的计划。

使用ci-base是更安全的选择。老版本的使用者更稀疏一些，bug会更加隐秘，发现得更晚。这种注意不到的bug会真正影响到策略的判断和迭代。而ci-base你在用，其他产品线也在用，问题很快能被发现和纠正。

如果正在运行的baidu-rpc版本被发现了用户有感的bug，我们会给每个baidu-rpc
server实例推送对应的bug信息，以提示产品线尽快升级，提示内容如下所示：

FATAL: 01-23 11:11:43: adx * 14542 src/baidu/rpc/trackme.cpp:141] Your baidu-rpc (r34140) is
affected by: [r34123-r34140] base::IOBuf::append(Movable) may leak memory;

### Q: SSL相关的链接问题

 在1.0.171.31586之**前**的版本中，可能出现如下的core，请在COMAKE的LDFLAGS中去掉-lssl
-lcrypto，加上-ldl -lz。原因在于baidu-rpc链接的third-64/openssl和系统提供的冲突。

在1.0.171.31586之**后**的版本中，为了提高对不同平台的兼容性，baidu-rpc不再静态链接openssl，可能在链接时出现undefined
reference，请在COMAKE的LDFLAGS中加上-lssl -lcrypto。

```
#0  0x000000302af6f950 in strcmp () from /lib64/tls/libc.so.6
#1  0x000000302cc992a3 in OBJ_NAME_new_index () from /lib64/libcrypto.so.4
#2  0x000000302cc959a0 in lh_free () from /lib64/libcrypto.so.4
#3  0x000000302cc95cc2 in lh_insert () from /lib64/libcrypto.so.4
#4  0x000000302cc99469 in OBJ_NAME_add () from /lib64/libcrypto.so.4
#5  0x000000000078a8a5 in SSL_library_init () at webfoot_item.cpp:21
#6  0x0000000000613b0d in RegisterAllExtensionsOrDieImpl () at
src/baidu/rpc/policy/register_all_extensions.cpp:97
#7  0x000000302b809a10 in pthread_once () from /lib64/tls/libpthread.so.0
#8  0x0000000000613a31 in baidu::rpc::policy::RegisterAllExtensionsOrDie () at
src/baidu/rpc/policy/register_all_extensions.cpp:238
#9  0x00000000005ccaac in Channel (this=0xb69c40) at src/baidu/rpc/channel.cpp:68
#10 0x000000000059e0f8 in __static_initialization_and_destruction_0 (__initialize_p=1,
__priority=65535) at test_ei_srv_client.cpp:8
#11 0x000000000059e159 in global constructors keyed to main () at test_ei_srv_client.cpp:19
#12 0x0000000000903696 in __do_global_ctors_aux () at ./src/base/spinlock.h:49
#13 0x000000000059b028 in _init ()
#14 0x0000000000903610 in __libc_csu_init () at ./src/base/spinlock.h:49
#15 0x00000000009035d1 in __libc_csu_init () at ./src/base/spinlock.h:49
#16 0x000000302af1c45f in __libc_start_main () from /lib64/tls/libc.so.6
#17 0x000000000059deaa in _start ()
#18 0x0000007fbffff1a8 in ?? ()
#19 0x000000000000001c in ?? () 
```

### Q: 为什么C++ client/server 能够互相通信， 和其他语言的client/server 通信会报序列化失败的错误

检查一下C++ 版本是否开启了压缩 (Controller::set_compress_type), 目前 python/JAVA
版的rpc框架还没有实现压缩，互相返回会出现问题。 

### Q: 两个产品线都使用protobuf，为什么不能互相访问

协议 !=
protobuf。protobuf负责打包，协议负责定字段。打包格式相同不意味着字段可以互通。协议中可能会包含多个protobuf包，以及额外的长度、校验码、magic
number等等。协议的互通是通过在RPC框架内转化为统一的编程接口完成的，而不是在protobuf层面。从广义上来说，protobuf也可以作为打包框架使用，生成其他序列化格式的包，像[idl
<=>
protobuf](http://wiki.baidu.com/pages/viewpage.action?pageId=144820547)就是通过protobuf生成了解析idl的代码。

### Q: protobuf打印了UTF-8相关的错误日志

```
// protobuf 2.4
google/protobuf/wire_format.cc:1059] Encountered string containing invalid UTF-8 data while parsing
protocol buffer. Strings must contain only UTF-8; use the 'bytes' type for raw bytes.
 
// protobuf 2.6
google/protobuf/wire_format.cc:1091] String field 'key' contains invalid UTF-8 data when serializing
a protocol buffer. Use the 'bytes' type if you intend to send raw bytes
```

原因：pb中的string必须是utf-8编码，非utf-8编码的字符串必须用bytes存储。

解决方式：

1. [推荐]
把proto中对应字段的类型从string改为bytes。string和bytes的二进制格式是一样的，所以这个改动不会造成新老消息的不兼容。这两个类型生成的函数也是一样的，用户代码不需要修改。
2. 定义宏NDEBUG。这个检查会被跳过。

注意：pb 2.4不会打印出问题的字段名，pb 2.6会，如果你需要快速定位出问题的字段，用pb 2.6
