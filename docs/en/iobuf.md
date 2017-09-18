brpc uses [butil::IOBuf](https://github.com/brpc/brpc/blob/master/src/butil/iobuf.h) as data structure for attachment storage and HTTP body. It is a non-contiguous zero copy buffer, which has been proved in other projects as excellent performance. The interface of `IOBuf` is similar to `std::string`, but not the same.

If you used the `BufHandle` in Kylin before, you should notice the difference in convenience of `IOBuf`: the former hardly had any encapsulation, leaving the internal structure directly in front of the user. The user  must carefully handle the reference count, which is very error prone, leading to lots of bugs.

# What IOBuf can：

- Default constructor doesn't involve copying.
- Explicit copy doesn't change source IOBuf. Only copy the management structure of IOBuf instead of the data.
- Append another IOBuf without copy.
- Append string involves copy.
- Read from/Write into fd.
- Convert to protobuf and vice versa.
- IOBufBuilder可以把IOBuf当std::ostream用。

# What IOBuf can't：

- Used as general storage structure. IOBuf should not keep a long life cycle to prevent multiple memory blocks (8K each) being locked by one IOBuf object.

# Slice

Slice 16 bytes from IOBuf：

```c++
source_buf.cut(&heading_iobuf, 16); // cut all bytes of source_buf when its length < 16
```

Remove 16 bytes：

```c++
source_buf.pop_front(16); // Empty source_buf when its length < 16
```

# Concatenate

Append to another IOBuf：

```c++
buf.append(another_buf);  // no data copy
```

Append std::string

```c++
buf.append(str);  // copy data of str into buf
```

# Parse

Parse protobuf from IOBuf 

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
pb_message.ParseFromZeroCopyStream(&wrapper);
```

Parse IOBuf as user-defined structure

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
CodedInputStream coded_stream(&wrapper);
coded_stream.ReadLittleEndian32(&value);
...
```

# Serialize

Serialize protobuf into IOBuf

```c++
IOBufAsZeroCopyOutputStream wrapper(&iobuf);
pb_message.SerializeToZeroCopyStream(&wrapper);
```

Append printable data into IOBuf

```c++
IOBufBuilder os;
os << "anything can be sent to std::ostream";
os.buf();  // IOBuf
```

# Print

```c++
std::cout << iobuf;
std::string str = iobuf.to_string();
```

# Performance

IOBuf has excellent performance in general aspects:

| Action                                   | Throughput  | QPS     |
| ---------------------------------------- | ----------- | ------- |
| Read from file -> Slice 12+16 bytes -> Copy -> Merge into another buffer ->Write to /dev/null | 240.423MB/s | 8586535 |
| Read from file -> Slice 12+128 bytes -> Copy-> Merge into another buffer ->Write to /dev/null | 790.022MB/s | 5643014 |
| Read from file -> Slice 12+1024 bytes -> Copy-> Merge into another buffer ->Write to /dev/null | 1519.99MB/s | 1467171 |