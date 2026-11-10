#!/usr/bin/env python3
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generate the seed corpus for fuzz_thrift.

Every seed is synthesised here from readable source -- there is no captured
traffic, so the corpus is kept as this script rather than as binary files.
oss-fuzz.sh runs it before packaging the corpus; to regenerate the seeds by
hand:

    python3 gen_thrift_seed_corpus.py [output-dir]
"""

import os
import struct
import sys

# Thrift message types (TMessageType in thrift/protocol/TBinaryProtocol.h).
CALL = 1
REPLY = 2
EXCEPTION = 3
ONEWAY = 4

# Thrift field types (TType in thrift/protocol/TBinaryProtocol.h).
T_STOP = 0x00
T_I16 = 0x06
T_I32 = 0x08
T_I64 = 0x0A
T_STRING = 0x0B
T_STRUCT = 0x0C
T_MAP = 0x0D
T_LIST = 0x0F

# Every struct -- including the top-level one that holds the arguments or
# results -- ends with this single stop-marker byte.
STOP = bytes([T_STOP])

# Strict binary-protocol version word: THRIFT_HEAD_VERSION_1 | message type.
# ParseThriftMessage() in src/brpc/policy/thrift_protocol.cpp rejects a frame
# unless (word & 0xffffff00) == 0x80010000, with the low byte as the type.
VERSION_1 = 0x8001


# ---------------------------------------------- thrift binary-protocol pieces
def field(ftype, fid, value):
    """One field: type byte, field id, then the encoded value."""
    return bytes([ftype]) + struct.pack(">h", fid) + value


def i16(value):
    return struct.pack(">h", value)


def i32(value):
    return struct.pack(">i", value)


def i64(value):
    return struct.pack(">q", value)


def tstr(text):
    encoded = text.encode()
    return struct.pack(">I", len(encoded)) + encoded


def tlist(elem_type, elements):
    return (bytes([elem_type]) + struct.pack(">I", len(elements))
            + b"".join(elements))


def tmap(key_type, value_type, pairs):
    return (bytes([key_type, value_type]) + struct.pack(">I", len(pairs))
            + b"".join(key + value for key, value in pairs))


def tstruct(*fields):
    """A struct body: zero or more fields closed by a stop marker."""
    return b"".join(fields) + STOP


def message_begin(name, mtype, seq_id):
    """Version word + method name + sequence id."""
    return (struct.pack(">I", (VERSION_1 << 16) | (mtype & 0xFF))
            + tstr(name) + struct.pack(">I", seq_id))


def frame(payload):
    """Wrap a thrift message in thrift framed's 4-byte size prefix."""
    return struct.pack(">I", len(payload)) + payload


# -------------------------------------------------------------------- the seeds
SEEDS = {}

# CALLs covering one argument encoding each: a scalar, a string, a homogeneous
# list, a map, and a struct that nests further fields.
SEEDS["call_simple"] = frame(
    message_begin("ping", CALL, 1) + field(T_I32, 1, i32(42)) + STOP)
SEEDS["call_str"] = frame(
    message_begin("echo", CALL, 2)
    + field(T_STRING, 2, tstr("hello")) + STOP)
SEEDS["call_list"] = frame(
    message_begin("sum", CALL, 3)
    + field(T_LIST, 3, tlist(T_I32, [i32(1), i32(2), i32(3)])) + STOP)
SEEDS["call_map"] = frame(
    message_begin("lookup", CALL, 4)
    + field(T_MAP, 4, tmap(T_STRING, T_I32,
                           [(tstr("k1"), i32(1)), (tstr("k2"), i32(2))]))
    + STOP)
SEEDS["call_nested"] = frame(
    message_begin("nested", CALL, 5)
    + field(T_STRUCT, 5, tstruct(
        field(T_I16, 1, i16(7)),
        field(T_I32, 2, i32(42)),
        field(T_I64, 3, i64(1234567890)),
        field(T_STRING, 4, tstr("inner"))))
    + STOP)

# The remaining message types, reusing the shapes above.
SEEDS["reply"] = frame(
    message_begin("ping", REPLY, 1) + field(T_I32, 1, i32(42)) + STOP)
SEEDS["oneway"] = frame(
    message_begin("notify", ONEWAY, 9)
    + field(T_STRING, 2, tstr("hello")) + STOP)
# An EXCEPTION reply with the wire shape of a serialised
# TApplicationException: a message string field and an error-code field.
SEEDS["exception"] = frame(
    message_begin("ping", EXCEPTION, 1)
    + field(T_STRING, 1, tstr("internal error"))
    + field(T_I32, 2, i32(1)) + STOP)

# A version word whose version bits are wrong (the low byte still says CALL),
# so ParseThriftMessage gives up on the stream and the other protocol parsers
# get to look at it instead.
_bad_payload = message_begin("ping", CALL, 1) + field(T_I32, 1, i32(42)) + STOP
SEEDS["bad_version"] = (struct.pack(">I", len(_bad_payload))
                        + struct.pack(">I", 0x0BAD0001) + _bad_payload[4:])

# Frame-level truncations.  "short" cuts the input off two bytes into the
# version word; "truncated_body" completes the 8-byte header but delivers none
# of the 16 bytes of body it advertises.  ParseThriftMessage must report "not
# enough data" for both instead of over-reading the buffer.
SEEDS["short"] = struct.pack(">I", 16) + b"\x80\x01"
SEEDS["truncated_body"] = (struct.pack(">I", 16)
                           + struct.pack(">I", (VERSION_1 << 16) | CALL))


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else "fuzz_thrift_seed_corpus"
    os.makedirs(out_dir, exist_ok=True)
    for name, seed in sorted(SEEDS.items()):
        with open(os.path.join(out_dir, name + ".bin"), "wb") as out:
            out.write(seed)
    print("Generated %d seeds in %s" % (len(SEEDS), out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))