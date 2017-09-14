brpc使用[butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h)作为存储附件或http body的数据结构，它是一种非连续零拷贝缓冲，在其他项目中得到了验证并有出色的性能。IOBuf的接口和std::string类似，但不相同。

如果你之前使用Kylin中的BufHandle，你将更能感受到IOBuf的便利性：前者几乎没有实现完整，直接暴露了内部结构，用户得小心翼翼地处理引用计数，极易出错。BufHandle是很多bug的诱因。

# IOBuf能做的：

- 默认构造不分配内存。
- 可以拷贝，修改拷贝不影响原IOBuf。拷贝的是IOBuf的管理结构而不是数据。
- 可以append另一个IOBuf，不拷贝数据。
- 可以append字符串，拷贝数据。
- 可以从fd读取，可以写入fd。
- 可以和protobuf相互转化。
- IOBufBuilder可以把IOBuf当std::ostream用。

# IOBuf不能做的：

- 程序内的通用存储结构。IOBuf应保持较短的生命周期，以避免一个IOBuf锁定了多个block (8K each)。

# 切割

切下16字节的IOBuf：

```c++
source_buf.cut(&heading_iobuf, 16); // 当source_buf不足16字节时，切掉所有字节。
```

跳过16字节：

```c++
source_buf.pop_front(16); // 当source_buf不足16字节时，清空它
```

# 拼接

append另一个IOBuf：

```c++
buf.append(another_buf);  // no data copy
```

append std::string

```c++
buf.append(str);  // copy data of str into buf
```

# 解析

解析IOBuf为protobuf

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

把可打印数据送入IOBuf

```c++
IOBufBuilder os;
os << "anything can be sent to std::ostream";
os.buf();  // IOBuf
```

# 打印

```c++
std::cout << iobuf;
std::string str = iobuf.to_string();
```

# 性能

IOBuf有很高的综合性能：

- 从文件读入->切割12+16字节->拷贝->合并到另一个缓冲->写出到/dev/null这一流程的吞吐是240.423MB/s或8586535次/s
- 从文件读入->切割12+128字节->拷贝->合并到另一个缓冲->写出到/dev/null这一流程的吞吐是790.022MB/s或5643014次/s
- 从文件读入->切割12+1024字节->拷贝->合并到另一个缓冲->写出到/dev/null这一流程的吞吐是1519.99MB/s或1467171次/s
