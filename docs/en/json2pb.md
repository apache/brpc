brpc supports **two-way** conversion between json and protobuf, implemented in [json2pb](https://github.com/brpc/brpc/tree/master/src/json2pb/), json parsing uses [rapidjson](https ://github.com/miloyip/rapidjson). This function is valid for both pb2.x and 3.x. pb3 has a built-in function of [conversion json](https://developers.google.com/protocol-buffers/docs/proto3#json).

By design, accessing the protobuf service through HTTP + json is a common way for external services, so the conversion must be accurate. The conversion rules are listed below.

## message

Corresponding to rapidjson Object, surrounded by curly braces, the elements in it will be parsed recursively.

```protobuf'
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
``

## repeated field

Corresponding to rapidjson Array, enclosed in square brackets, the elements in it will be parsed recursively. Unlike message, each element has the same type.

```protobuf'
// protobuf
repeated int32 numbers = 1;

// rapidjson
{"numbers" : [12, 17, 1, 24] }
``

## map

Repeated MSG that meets the following conditions is regarded as a json map:

-MSG contains a field named key, the type is string, and the tag is 1.
-MSG contains a field named value and tag is 2.
-No other fields are included.

The attributes of this "map" are:

-Naturally, it is not possible to ensure that the keys are orderly or non-repetitive, and users can check on their own as required.
-It is binary compatible with the map in protobuf 3.x, so the map in 3.x using pb2json will also be converted to json map correctly.

If the repeated MSG that meets all the conditions does not need to be considered a json map, just break any of the above conditions: add optional int32 this_message_is_not_map_entry = 3; this method destroys the "do not include other fields" item, and does not affect Binary compatibility. You can also swap the tag values ​​of key and value, let the former be 2 and the latter be 1, so that the condition is no longer satisfied.

## integers

rapidjson will mark the corresponding type according to the value, such as:

* For 3, the IsUInt, IsInt, IsUint64, IsInt64 and other functions in rapidjson will all return true.
* For -1, IsUInt and IsUint64 will return false.
* For 5000000000, IsUInt and IsInt are false.

This allows us to automatically fill UInt in json into int64 in protobuf without special processing, instead of mechanically thinking that the two types do not match. Correspondingly, the conversion code can naturally recognize overflow and underflow, and the conversion will fail when it appears.

```protobuf'
// protobuf
int32 uint32 int64 uint64

// rapidjson
Int UInt Int64 UInt64
``

## floating point

The integer type of json can also be transferred to the floating point type of pb. In addition to ordinary numbers, floating-point numbers (IEEE754) also accept three strings: "NaN", "Infinity", and "-Infinity", corresponding to Not A Number, positive infinity and negative infinity.

```protobuf'
// protobuf
float double

// rapidjson
Float Double Int Uint Int64 Uint64
``

## enum

Enum can be converted into an integer or the string corresponding to its name, which can be controlled by Pb2JsonOptions.enum_options. The latter is the default.

## string

The default conversion with the same name. But when an illegal C++ variable name appears in json (the variable name rule of pb), conversion is allowed. The rule is:

`illegal-char <-> **_Z**<ASCII-of-the-char>**_**`

## bytes

Unlike string, bytes that may contain \0 are encoded in base64 by default.

```protobuf'
// protobuf
"Hello, World!"

// json
"SGVsbG8sIFdvcmxkIQo ="
``

## bool

Corresponding to true false of json

## unknown fields

unknown_fields → json is not currently supported, and may be supported in the future. json → unknown_fields is currently not supported, that is, protobuf cannot transparently transmit fields that are not recognized in json. The reason is that the real key of protobuf is the number after each field in the proto file:

```protobuf'
...
required int32 foo = 3; <-- the real key
...
``

This is also the key of unknown_fields. When a protobuf does not recognize a certain field, there must be no that number in its proto, so there is no way to insert unknown_fields.

There are several feasible solutions:

-Ensure that the proto file of the service accessed by json is up to date. In this way, there is no need for transparent transmission, but the more the front-end service is, the more similar the proxy is, which may not be realistic.
-Special transparent transmission fields are defined in protobuf. For example, the name is unknown_json_fields, which is specially processed when parsing the corresponding protobuf. This program has a wide range of modifications and has a certain impact on performance, and it will be discussed when there is a clear demand.