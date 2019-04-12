```
[mysql](https://www.mysql.com/)是著名的开源的关系型数据库，为了使用户更快捷地访问mysql并充分利用bthread的并发能力，brpc直接支持mysql协议。示例程序：[example/mysql_c++](https://github.com/brpc/brpc/tree/master/example/mysql_c++/)

**注意**：只支持MySQL 4.1 及之后的版本的文本协议，支持事务，不支持Prepared statement。目前支持的鉴权方式为mysql_native_password

相比使用[libmysqlclient](https://dev.mysql.com/downloads/connector/c/)(官方client)的优势有：

- 线程安全。用户不需要为每个线程建立独立的client。
- 支持同步、异步、半同步等访问方式，能使用[ParallelChannel等](combo_channel.md)组合访问方式。
- 支持多种[连接方式](client.md#连接方式)。支持超时、backup request、取消、tracing、内置服务等一系列brpc提供的福利。
- 明确的返回类型校验，如果使用了不正确的变量接受mysql的数据类型，将抛出异常。
- 调用mysql标准库会阻塞框架的并发能力，使用本实现将能充分利用brpc框架的并发能力。
# 访问mysql

创建一个访问mysql的Channel：

​```c++
#include <brpc/mysql.h>
#include <brpc/policy/mysql_authenticator.h>
#include <brpc/channel.h>
 
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
... 
​```

向mysql发起命令。

​```c++
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
...
​```

上述代码的说明：

- 请求类型必须为MysqlRequest，回复类型必须为MysqlResponse，否则CallMethod会失败。不需要stub，直接调用channel.CallMethod，method填NULL。
- 调用request.Query()传入要执行的命令，可以批量执行命令，多个命令用分号隔开。
- 依次调用response.reply(X)弹出操作结果，根据返回类型的不同，选择不同的类型接收，如：MysqlReply::Ok，MysqlReply::Error，const MysqlReply::Columnconst MysqlReply::Row等。
- 如果只有一条命令则reply为1个，如果为批量操作返回的reply为多个。

目前支持的请求操作有：

​```c++
bool Query(const butil::StringPiece& command);
​```

对应的回复操作：

​```c++
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
const MysqlReply::Field& MysqlReply::Row::field(const uint64_t index) const
​```

事务的操作
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

```

