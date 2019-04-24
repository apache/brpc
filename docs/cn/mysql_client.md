[mysql](https://www.mysql.com/)是著名的开源的关系型数据库，为了使用户更快捷地访问mysql并充分利用bthread的并发能力，brpc直接支持mysql协议。示例程序：[example/mysql_c++](https://github.com/brpc/brpc/tree/master/example/mysql_c++/)

**注意**：只支持MySQL 4.1 及之后的版本的文本协议，支持事务，不支持Prepared statement。目前支持的鉴权方式为mysql_native_password，使用事务的时候不支持single模式。

相比使用[libmysqlclient](https://dev.mysql.com/downloads/connector/c/)(官方client)的优势有：

- 线程安全。用户不需要为每个线程建立独立的client。
- 支持同步、异步、半同步等访问方式，能使用[ParallelChannel等](combo_channel.md)组合访问方式。
- 支持多种[连接方式](client.md#连接方式)。支持超时、backup request、取消、tracing、内置服务等一系列brpc提供的福利。
- 明确的返回类型校验，如果使用了不正确的变量接受mysql的数据类型，将抛出异常。
- 调用mysql标准库会阻塞框架的并发能力，使用本实现将能充分利用brpc框架的并发能力。
- 使用brpc实现的mysql不会造成pthread的阻塞，使用libmysqlclient会阻塞pthread [线程相关](bthread.md)。
# 访问mysql

创建一个访问mysql的Channel：

```c++
# include <brpc/mysql.h>
# include <brpc/policy/mysql_authenticator.h>
# include <brpc/channel.h>

brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_MYSQL;
options.connection_type = FLAGS_connection_type;
options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
options.max_retry = FLAGS_max_retry;
options.auth = new brpc::policy::MysqlAuthenticator("yangliming01", "123456", "test", 
    "charset=utf8&collation_connection=utf8_unicode_ci");
if (channel.Init("127.0.0.1", 3306, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel";
    return -1;
}
```

向mysql发起命令。

```c++
// 执行各种mysql命令，可以批量执行命令如："select * from tab1;select * from tab2"
std::string command = "show databases"; // select,delete,update,insert,create,drop ...
brpc::MysqlRequest request;
if (!request.Query(command)) {
    LOG(ERROR) << "Fail to add command";
    return false;
}
brpc::MysqlResponse response;
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);
if (!cntl.Failed()) {
    std::cout << response << std::endl;
} else {
    LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
    return false;
}
return true;
```

上述代码的说明：

- 请求类型必须为MysqlRequest，回复类型必须为MysqlResponse，否则CallMethod会失败。不需要stub，直接调用channel.CallMethod，method填NULL。
- 调用request.Query()传入要执行的命令，可以批量执行命令，多个命令用分号隔开。
- 依次调用response.reply(X)弹出操作结果，根据返回类型的不同，选择不同的类型接收，如：MysqlReply::Ok，MysqlReply::Error，const MysqlReply::Columnconst MysqlReply::Row等。
- 如果只有一条命令则reply为1个，如果为批量操作返回的reply为多个。

目前支持的请求操作有：

```c++
bool Query(const butil::StringPiece& command);
```

对应的回复操作：

```c++
// 返回不同类型的结果
const MysqlReply::Auth& auth() const;
const MysqlReply::Ok& ok() const;
const MysqlReply::Error& error() const;
const MysqlReply::Eof& eof() const;
// 对result set结果集的操作
// get column number
uint64_t MysqlReply::column_number() const;
// get one column
const MysqlReply::Column& MysqlReply::column(const uint64_t index) const;
// get row number
uint64_t MysqlReply::row_number() const;
// get one row
const MysqlReply::Row& MysqlReply::next() const;
// 结果集中每个字段的操作
const MysqlReply::Field& MysqlReply::Row::field(const uint64_t index) const;
```

# 事务操作

```c++
 rpc::Channel channel;
// Initialize the channel, NULL means using default options.
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_MYSQL;
options.connection_type = FLAGS_connection_type;
options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
options.connect_timeout_ms = FLAGS_connect_timeout_ms;
options.max_retry = FLAGS_max_retry;
options.auth = new brpc::policy::MysqlAuthenticator(
    FLAGS_user, FLAGS_password, FLAGS_schema, FLAGS_params);
if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel";
    return -1;
}

// create transaction
brpc::MysqlTransactionOptions options;
options.readonly = FLAGS_readonly;
options.isolation_level = brpc::MysqlIsolationLevel(FLAGS_isolation_level);
auto tx(brpc::NewMysqlTransaction(channel, options));
if (tx == NULL) {
    LOG(ERROR) << "Fail to create transaction";
    return false;
}

brpc::MysqlRequest request(tx.get());
if (!request.Query(*it)) {
    LOG(ERROR) << "Fail to add command";
    tx->rollback();
    return false;
}
brpc::MysqlResponse response;
brpc::Controller cntl;
channel.CallMethod(NULL, &cntl, &request, &response, NULL);
if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access mysql, " << cntl.ErrorText();
    tx->rollback();
    return false;
}
// handle response
std::cout << response << std::endl;
bool rc = tx->commit();
```

# 性能测试

我在example/mysql_c++目录下面写了两个测试程序，mysql_press.cpp mysqlclient_press.cpp，mysql_go_press.go 一个是使用了brpc框架，一个是使用了的libmysqlclient访问mysql，一个是使用[go-sql-driver](https://github.com/go-sql-driver)/**go-mysql**访问mysql

启动单线程测试

##### brpc框架访问mysql（单线程）

./mysql_press -thread_num=1 -op_type=0 // insert

```
qps=3071 latency=320
qps=3156 latency=311
qps=3166 latency=310
qps=3151 latency=312
qps=3093 latency=317
qps=3146 latency=312
qps=3139 latency=313
qps=3114 latency=315
qps=3055 latency=321
qps=3135 latency=313
qps=2611 latency=376
qps=3072 latency=320
qps=3026 latency=324
qps=2792 latency=352
qps=3181 latency=309
qps=3181 latency=309
qps=3197 latency=307
qps=3024 latency=325
```

./mysql_press -thread_num=1 -op_type=1

```
qps=6414 latency=151
qps=5292 latency=182
qps=6700 latency=144
qps=6858 latency=141
qps=6915 latency=140
qps=6822 latency=142
qps=6722 latency=144
qps=6852 latency=141
qps=6713 latency=144
qps=6741 latency=144
qps=6734 latency=144
qps=6611 latency=146
qps=6554 latency=148
qps=6810 latency=142
qps=6787 latency=143
qps=6737 latency=144
qps=6579 latency=147
qps=6634 latency=146
qps=6716 latency=144
qps=6711 latency=144
```

./mysql_press -thread_num=1 -op_type=2 // update

```
qps=3090 latency=318
qps=3452 latency=284
qps=3239 latency=303
qps=3328 latency=295
qps=3218 latency=305
qps=3251 latency=302
qps=2516 latency=391
qps=2874 latency=342
qps=3366 latency=292
qps=3249 latency=302
qps=3346 latency=294
qps=3486 latency=282
qps=3457 latency=284
qps=3439 latency=286
qps=3386 latency=290
qps=3352 latency=293
qps=3253 latency=302
qps=3341 latency=294
```

##### libmysqlclient实现（单线程）

./mysqlclient_press -thread_num=1 -op_type=0 // insert

```
qps=3166 latency=313
qps=3157 latency=314
qps=2941 latency=337
qps=3270 latency=303
qps=3305 latency=300
qps=3445 latency=287
qps=3455 latency=287
qps=3449 latency=287
qps=3486 latency=284
qps=3551 latency=279
qps=3517 latency=281
qps=3283 latency=302
qps=3353 latency=295
qps=2564 latency=386
qps=3243 latency=305
qps=3333 latency=297
qps=3598 latency=275
qps=3714 latency=267
```

./mysqlclient_press -thread_num=1 -op_type=1

```
qps=8209 latency=120
qps=8022 latency=123
qps=7879 latency=125
qps=8083 latency=122
qps=8504 latency=116
qps=8112 latency=121
qps=8278 latency=119
qps=8698 latency=113
qps=8817 latency=112
qps=8755 latency=112
qps=8734 latency=113
qps=8390 latency=117
qps=8230 latency=120
qps=8486 latency=116
qps=8038 latency=122
qps=8640 latency=114
```

./mysqlclient_press -thread_num=1 -op_type=2 // update

```
qps=3583 latency=276
qps=3530 latency=280
qps=3610 latency=274
qps=3492 latency=283
qps=3508 latency=282
qps=3465 latency=286
qps=3543 latency=279
qps=3610 latency=274
qps=3567 latency=278
qps=3381 latency=293
qps=3514 latency=282
qps=3461 latency=286
qps=3456 latency=286
qps=3517 latency=281
qps=3492 latency=284
```

##### golang访问mysql（单线程）

go run test.go -thread_num=1

```
qps = 6905 latency = 144
qps = 6922 latency = 143
qps = 6931 latency = 143
qps = 6998 latency = 142
qps = 6780 latency = 146
qps = 6980 latency = 142
qps = 6901 latency = 144
qps = 6887 latency = 144
qps = 6943 latency = 143
qps = 6880 latency = 144
qps = 6815 latency = 146
qps = 6089 latency = 163
qps = 6626 latency = 150
qps = 6361 latency = 156
qps = 6783 latency = 146
qps = 6789 latency = 146
qps = 6883 latency = 144
qps = 6795 latency = 146
qps = 6724 latency = 148
qps = 6861 latency = 145
qps = 6878 latency = 144
qps = 6842 latency = 146
```

从以上测试结果看来，使用brpc实现的mysql协议和使用libmysqlclient在插入、修改、删除操作上性能是类似的，但是在查询操作看会逊色于libmysqlclient，查询的性能和golang实现的mysql类似。

##### brpc框架访问mysql（50线程）

./mysql_press -thread_num=50 -op_type=1 -use_bthread=true

```
qps=18843 latency=2656
qps=22426 latency=2226
qps=22536 latency=2203
qps=22560 latency=2193
qps=22270 latency=2226
qps=22302 latency=2247
qps=22147 latency=2225
qps=22517 latency=2228
qps=22762 latency=2176
qps=23061 latency=2162
qps=23819 latency=2070
qps=23852 latency=2077
qps=22682 latency=2214
qps=22381 latency=2213
qps=24041 latency=2069
qps=24562 latency=2022
qps=24874 latency=2004
qps=24821 latency=1988
qps=24209 latency=2073
qps=21706 latency=2281
```

##### libmysqlclient实现（50线程）

./mysql_press -thread_num=50 -op_type=1 -use_bthread=true

```
qps=23656 latency=378
qps=16190 latency=555
qps=20136 latency=445
qps=22238 latency=401
qps=22229 latency=403
qps=19109 latency=470
qps=22569 latency=394
qps=26250 latency=343
qps=28208 latency=318
qps=29649 latency=301
qps=29874 latency=301
qps=30033 latency=301
qps=25911 latency=345
qps=28048 latency=317
qps=27398 latency=329
```

##### golang访问mysql（50协程）

go run ../mysql_go_press.go -thread_num=50

```
qps = 23660 latency = 2049
qps = 23198 latency = 2160
qps = 23765 latency = 2181
qps = 23323 latency = 2149
qps = 14833 latency = 2136
qps = 23822 latency = 2853
qps = 20389 latency = 2474
qps = 23290 latency = 2151
qps = 23526 latency = 2153
qps = 21426 latency = 2613
qps = 23339 latency = 2155
qps = 25623 latency = 2084
qps = 23048 latency = 2210
qps = 20694 latency = 2423
qps = 23705 latency = 2122
qps = 23445 latency = 2125
qps = 24368 latency = 2054
qps = 23027 latency = 2175
qps = 24307 latency = 2063
qps = 23227 latency = 2096
qps = 23646 latency = 2173
```

以上是启动50并发的查询请求，看上去qps都比较相似，但是libmysqlclient延时明显低。

##### brpc框架访问mysql（100线程）

./mysql_press -thread_num=100 -op_type=1 -use_bthread=true

```
qps=26428 latency=3764
qps=26305 latency=3780
qps=26390 latency=3779
qps=26278 latency=3787
qps=26326 latency=3787
qps=26266 latency=3792
qps=26394 latency=3773
qps=26263 latency=3797
qps=26250 latency=3783
qps=26362 latency=3782
qps=26212 latency=3796
qps=26260 latency=3800
qps=24666 latency=4035
qps=25569 latency=3896
qps=26223 latency=3794
qps=25538 latency=3890
qps=20065 latency=4958
qps=23023 latency=4331
qps=25808 latency=3875
```

##### libmysqlclient实现（100线程）

./mysql_press -thread_num=50 -op_type=1 -use_bthread=true

```
qps=29467 latency=304
qps=29413 latency=305
qps=29459 latency=304
qps=29562 latency=302
qps=30657 latency=291
qps=30445 latency=295
qps=30179 latency=298
qps=30072 latency=297
qps=29802 latency=299
qps=29752 latency=301
qps=29701 latency=304
qps=29731 latency=301
qps=29622 latency=299
qps=29440 latency=304
qps=29495 latency=306
qps=29297 latency=303
qps=29626 latency=306
qps=29482 latency=300
qps=28649 latency=313
qps=29537 latency=305
qps=29634 latency=299
```

##### golang访问mysql（100协程）

go run ../mysql_go_press.go -thread_num=100

```
qps = 22108 latency = 4553
qps = 21930 latency = 4536
qps = 20653 latency = 4906
qps = 22100 latency = 4443
qps = 21091 latency = 4850
qps = 21718 latency = 4600
qps = 21444 latency = 4488
qps = 17832 latency = 5859
qps = 18296 latency = 5378
qps = 20463 latency = 4963
qps = 21611 latency = 4880
qps = 18441 latency = 5424
qps = 20731 latency = 4834
qps = 20611 latency = 4837
qps = 20188 latency = 4979
qps = 15450 latency = 5723
qps = 20927 latency = 5328
qps = 19893 latency = 5027
qps = 21080 latency = 4782
qps = 20192 latency = 4970
```

以上是启动100并发的查询请求，看上去qps都比较相似，但是libmysqlclient延时明显低。

并发调整到150的时候，mysql-server已经报错"Too many connections"。

为什么并发数50或者100的时候libmysqlclient的延时会那么低呢？因为libmysqlclient使用的IO模式为阻塞模式，我们运行的mysql_press和mysqlclient_press都是使用的bthread模式（-use_bthread=true），底层默认都是9个pthread，使用阻塞模式的libmysqlclient和mysql交互的相当于并发度是9个线程，mysql会启动9个线程，使用非阻塞模式的rpc访问mysql并发度相当于100个，mysql会启动100个线程，所以会造成mysql的频繁上线文切换。

如果将libmysqlclient的执行方式改为不使用bthread，那么100个线程的执行效果为如下：

```
qps=26919 latency=1927
qps=27155 latency=2037
qps=28054 latency=1784
qps=26738 latency=1856
qps=27807 latency=1781
qps=26734 latency=1730
qps=26562 latency=1939
qps=27473 latency=1845
qps=26677 latency=1806
qps=27369 latency=1948
qps=27955 latency=1618
qps=26574 latency=2151
qps=27343 latency=1777
qps=26705 latency=1822
qps=26668 latency=1807
qps=25347 latency=2104
qps=26651 latency=1560
qps=27815 latency=1979
qps=27221 latency=1762
qps=26516 latency=2017
```

这个结果就和brpc框架启动100个bthread访问mysql的效果类似了。

##### 内存使用

在内存占用上，mysql_press和mysqlclient_press都运行了一个晚上，两个程序的内存占用

![libmysqlclient](../images/mysql_memory.png)



以上为我的一些简单测试，以及一些简单的分析，在低并发的情况下同步IO的效率高于异步IO，可以阅读[IO相关的内容](io.md)有更多解释，后续还将继续分析性能问题，优化协议，给出更多测试。