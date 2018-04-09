brpc支持json和protobuf间的**双向**转化，实现于[json2pb](https://github.com/brpc/brpc/tree/master/src/json2pb/)，json解析使用[rapidjson](https://github.com/miloyip/rapidjson)。此功能对pb2.x和3.x均有效。pb3内置了[转换json](https://developers.google.com/protocol-buffers/docs/proto3#json)的功能。

by design, 通过HTTP + json访问protobuf服务是对外服务的常见方式，故转化必须精准，转化规则列举如下。

## message

 对应rapidjson Object, 以花括号包围，其中的元素会被递归地解析。

```protobuf
// protobuf
message Foo {
    required string field1 = 1;
    required int32 field2 = 2;  
}
message Bar { 
    required Foo foo = 1; 
    optional bool flag = 2;
    required string name = 3;
}

// rapidjson
{"foo":{"field1":"hello", "field2":3},"name":"Tom" }
```

## repeated field

对应rapidjson Array, 以方括号包围，其中的元素会被递归地解析，和message不同，每个元素的类型相同。

```protobuf
// protobuf
repeated int32 numbers = 1;

// rapidjson
{"numbers" : [12, 17, 1, 24] }
```

## map

满足如下条件的repeated MSG被视作json map :

- MSG包含一个名为key的字段，类型为string，tag为1。
- MSG包含一个名为value的字段，tag为2。
- 不包含其他字段。

这种"map"的属性有：

- 自然不能确保key有序或不重复，用户视需求自行检查。
- 与protobuf 3.x中的map二进制兼容，故3.x中的map使用pb2json也会正确地转化为json map。

如果符合所有条件的repeated MSG并不需要被认为是json map，打破上面任一条件就行了: 在MSG中加入optional int32 this_message_is_not_map_entry = 3; 这个办法破坏了“不包含其他字段”这项，且不影响二进制兼容。也可以调换key和value的tag值，让前者为2后者为1，也使条件不再满足。

## integers

rapidjson会根据值打上对应的类型标记，比如：

* 对于3，rapidjson中的IsUInt, IsInt, IsUint64, IsInt64等函数均会返回true。
* 对于-1，则IsUInt和IsUint64会返回false。
* 对于5000000000，IsUInt和IsInt是false。

这使得我们不用特殊处理，转化代码就可以自动地把json中的UInt填入protobuf中的int64，而不是机械地认为这两个类型不匹配。相应地，转化代码自然能识别overflow和underflow，当出现时会转化失败。

```protobuf
// protobuf
int32 uint32 int64 uint64

// rapidjson
Int UInt Int64 UInt64
```

## floating point

json的整数类型也可以转至pb的浮点数类型。浮点数(IEEE754)除了普通数字外还接受"NaN", "Infinity", "-Infinity"三个字符串，分别对应Not A Number，正无穷，负无穷。

```protobuf
// protobuf
float double

// rapidjson
Float Double Int Uint Int64 Uint64
```

## enum

enum可转化为整数或其名字对应的字符串，可由Pb2JsonOptions.enum_options控制。默认后者。

## string

默认同名转化。但当json中出现非法C++变量名（pb的变量名规则）时，允许转化，规则是:

`illegal-char <-> **_Z**<ASCII-of-the-char>**_**`

## bytes

和string不同，可能包含\0的bytes默认以base64编码。

```protobuf
// protobuf
"Hello, World!"

// json
"SGVsbG8sIFdvcmxkIQo="
```

## bool

对应json的true false

## unknown fields

unknown_fields → json目前不支持，未来可能支持。json → unknown_fields目前也未支持，即protobuf无法透传json中不认识的字段。原因在于protobuf真正的key是proto文件中每个字段后的数字:

```protobuf
...
required int32 foo = 3; <-- the real key
...
```

这也是unknown_fields的key。当一个protobuf不认识某个字段时，其proto中必然不会有那个数字，所以没办法插入unknown_fields。

可行的方案有几种：

- 确保被json访问的服务的proto文件最新。这样就不需要透传了，但越前端的服务越类似proxy，可能并不现实。
- protobuf中定义特殊透传字段。比如名为unknown_json_fields，在解析对应的protobuf时特殊处理。此方案修改面广且对性能有一定影响，有明确需求时再议。
