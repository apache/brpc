[English version](../en/iobuf.md)

brpc使用[butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h)作为一些协议中的附件或http body的数据结构，它是一种非连续零拷贝缓冲，在其他项目中得到了验证并有出色的性能。IOBuf的接口和std::string类似，但不相同。

如果你之前使用Kylin中的BufHandle，你将更能感受到IOBuf的便利性：前者几乎没有实现完整，直接暴露了内部结构，用户得小心翼翼地处理引用计数，极易出错。

# IOBuf能做的：

- 默认构造不分配内存。
- 可以拷贝，修改拷贝不影响原IOBuf。拷贝的是IOBuf的管理结构而不是数据。
- 可以append另一个IOBuf，不拷贝数据。
- 可以append字符串，拷贝数据。
- 可以从fd读取，可以写入fd。
- 可以解析或序列化为protobuf messages.
- IOBufBuilder可以把IOBuf当std::ostream用。

# IOBuf不能做的：

- 程序内的通用存储结构。IOBuf应保持较短的生命周期，以避免一个IOBuf锁定了多个block (8K each)。

# 切割

从source_buf头部切下16字节放入dest_buf：

```c++
source_buf.cut(&dest_buf, 16); // 当source_buf不足16字节时，切掉所有字节。
```

从source_buf头部弹掉16字节：

```c++
source_buf.pop_front(16); // 当source_buf不足16字节时，清空它
```

# 拼接

在尾部加入另一个IOBuf：

```c++
buf.append(another_buf);  // no data copy
```

在尾部加入std::string

```c++
buf.append(str);  // copy data of str into buf
```

# 解析

解析IOBuf为protobuf message

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
pb_message.ParseFromZeroCopyStream(&wrapper);
```

解析IOBuf为自定义结构

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
CodedInputStream coded_stream(&wrapper);
coded_stream.ReadLittleEndian32(&value);
...
```

# 序列化

protobuf序列化为IOBuf

```c++
IOBufAsZeroCopyOutputStream wrapper(&iobuf);
pb_message.SerializeToZeroCopyStream(&wrapper);
```

用可打印数据创建IOBuf

```c++
IOBufBuilder os;
os << "anything can be sent to std::ostream";
os.buf();  // IOBuf
```

# 打印

可直接打印至std::ostream. 注意这个例子中的iobuf必需只包含可打印字符。

```c++
std::cout << iobuf << std::endl;
// or
std::string str = iobuf.to_string(); // 注意: 会分配内存
printf("%s\n", str.c_str());
```

# 性能

IOBuf有不错的综合性能：

| 动作                                       | 吞吐          | QPS     |
| ---------------------------------------- | ----------- | ------- |
| 文件读入->切割12+16字节->拷贝->合并到另一个缓冲->写出到/dev/null | 240.423MB/s | 8586535 |
| 文件读入->切割12+128字节->拷贝->合并到另一个缓冲->写出到/dev/null | 790.022MB/s | 5643014 |
| 文件读入->切割12+1024字节->拷贝->合并到另一个缓冲->写出到/dev/null | 1519.99MB/s | 1467171 |
