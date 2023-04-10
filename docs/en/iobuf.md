[中文版](../cn/iobuf.md)

brpc uses [butil::IOBuf](https://github.com/apache/brpc/blob/master/src/butil/iobuf.h) as data structure for attachment in some protocols and HTTP body. It's a non-contiguous zero-copied buffer, proved in previous projects, and good at performance. The interface of `IOBuf` is similar to `std::string`, but not the same.

If you've used the `BufHandle` in Kylin before, you should notice the convenience of `IOBuf`: the former one is badly encapsulated, leaving the internal structure directly in front of users, who must carefully handle the referential countings, very error prone and leading to bugs.

# What IOBuf can:

- Default constructor does not allocate memory.
- Copyable. Modifications to the copy doesn't affect the original one. Copy the managing structure of IOBuf only rather the payload.
- Append another IOBuf without copying payload.
- Can append string, by copying payload.
- Read from or write into file descriptors.
- Serialize to or parse from protobuf messages.
- constructible like a std::ostream using IOBufBuilder.

# What IOBuf can't:

- Used as universal string-like structure in the program. Lifetime of IOBuf should be short, to prevent the referentially counted blocks(8K each) in IOBuf lock too many memory.

# Cut

Cut 16 bytes from front-side of source_buf and append to dest_buf:

```c++
source_buf.cut(&dest_buf, 16); // cut all bytes of source_buf when its length < 16
```

Just pop 16 bytes from front-side of source_buf:

```c++
source_buf.pop_front(16); // Empty source_buf when its length < 16
```

# Append

Append another IOBuf to back-side：

```c++
buf.append(another_buf);  // no data copy
```

Append std::string to back-sie

```c++
buf.append(str);  // copy data of str into buf
```

# Parse

Parse a protobuf message from the IOBuf 

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
pb_message.ParseFromZeroCopyStream(&wrapper);
```

Parse IOBuf in user-defined formats

```c++
IOBufAsZeroCopyInputStream wrapper(&iobuf);
CodedInputStream coded_stream(&wrapper);
coded_stream.ReadLittleEndian32(&value);
...
```

# Serialize

Serialize a protobuf message into the IOBuf

```c++
IOBufAsZeroCopyOutputStream wrapper(&iobuf);
pb_message.SerializeToZeroCopyStream(&wrapper);
```

Built IOBuf with printable data

```c++
IOBufBuilder os;
os << "anything can be sent to std::ostream";
os.buf();  // IOBuf
```

# Print

Directly printable to std::ostream. Note that the iobuf in following example should only contain printable characters.

```c++
std::cout << iobuf << std::endl;
// or
std::string str = iobuf.to_string(); // note: allocating memory
printf("%s\n", str.c_str());
```

# Performance

IOBuf is good at performance:

| Action                                   | Throughput  | QPS     |
| ---------------------------------------- | ----------- | ------- |
| Read from file -> Cut 12+16 bytes -> Copy -> Merge into another buffer ->Write to /dev/null | 240.423MB/s | 8586535 |
| Read from file -> Cut 12+128 bytes -> Copy-> Merge into another buffer ->Write to /dev/null | 790.022MB/s | 5643014 |
| Read from file -> Cut 12+1024 bytes -> Copy-> Merge into another buffer ->Write to /dev/null | 1519.99MB/s | 1467171 |
