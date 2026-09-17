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
"""Generate the seed corpus for fuzz_rtmp.

Every seed is synthesised here from readable source -- there is no captured
traffic and no third-party media, so the corpus is kept as this script rather
than as binary files.  oss-fuzz.sh runs it before packaging the corpus; to
regenerate the seeds by hand:

    python3 gen_rtmp_seed_corpus.py [output-dir]
"""

import os
import struct
import sys

# A simplified-RTMP stream: version byte 3 followed by
# SIMPLIFIED_RTMP_MAGIC_NUMBER, see src/brpc/policy/rtmp_protocol.cpp.
MAGIC = b"\x03BDMS"


# ----------------------------------------------------------------- AMF0 values
def num(value):
    return b"\x00" + struct.pack(">d", value)


def boolean(value):
    return b"\x01" + (b"\x01" if value else b"\x00")


def string(value):
    encoded = value.encode()
    return b"\x02" + struct.pack(">H", len(encoded)) + encoded


def null():
    return b"\x05"


def obj(pairs):
    out = b"\x03"
    for key, value in pairs:
        encoded = key.encode()
        out += struct.pack(">H", len(encoded)) + encoded + value
    return out + b"\x00\x00\x09"  # object end marker


# -------------------------------------------------------- RTMP chunk encoding
def basic_header(fmt, cs_id):
    if cs_id < 64:
        return bytes([(fmt << 6) | cs_id])
    if cs_id < 320:
        return bytes([(fmt << 6) | 0, cs_id - 64])
    return bytes([(fmt << 6) | 1]) + struct.pack("<H", cs_id - 64)


def u24(value):
    return struct.pack(">I", value)[1:]


def chunk(cs_id, payload, fmt=0, ts=0, type_id=0, stream_id=0, length=None):
    """One RTMP chunk.

    `length` overrides the declared message length, which lets a seed advertise
    a size that disagrees with the bytes that actually follow it.
    """
    declared = len(payload) if length is None else length
    header = basic_header(fmt, cs_id)
    if fmt == 0:
        header += (u24(ts) + u24(declared) + bytes([type_id])
                   + struct.pack("<I", stream_id))
    elif fmt == 1:
        header += u24(ts) + u24(declared) + bytes([type_id])
    elif fmt == 2:
        header += u24(ts)
    return header + payload


def command(name, txid, *args, **kwargs):
    """An AMF command message (type 20 = AMF0, type 17 = AMF3)."""
    return chunk(kwargs.pop("cs_id", 3),
                 string(name) + num(txid) + b"".join(args),
                 type_id=kwargs.pop("type_id", 20),
                 stream_id=kwargs.pop("stream_id", 1),
                 **kwargs)


def user_control(event, value):
    return chunk(2, struct.pack(">HI", event, value), type_id=4)


def corrupt(buf, offset, byte):
    """Splice a stray byte in *without* correcting the declared message length.

    This models a mutated seed, and keeps the corruption visible in the source
    instead of hiding it inside an opaque blob.
    """
    return buf[:offset] + bytes([byte]) + buf[offset:]


# ---------------------------------------------------------------- AMF payloads
CONNECT_ARGS = obj([
    ("app", string("live")),
    ("flashVer", string("FMLE/3.0 (compatible; FMSc/1.0)")),
    ("tcUrl", string("rtmp://127.0.0.1/live")),
    ("fpad", boolean(False)),
    ("capabilities", num(15)),
    ("audioCodecs", num(4071)),
    ("videoCodecs", num(252)),
    ("videoFunction", num(1)),
    ("objectEncoding", num(0)),
])

METADATA = string("@setDataFrame") + string("onMetaData") + obj([
    ("duration", num(0)),
    ("width", num(1280)),
    ("height", num(720)),
    ("videocodecid", num(7)),
    ("audiocodecid", num(10)),
])

# An AVC sequence header (SPS/PPS) and one IDR NALU.  These stay as literals
# because they are opaque codec bytes that the code under test only forwards.
AVC_SEQ_HEADER = bytes.fromhex(
    "17000000000142c01effe100096742c01ed90089f96601000468ce3c80")
AVC_NALU = bytes.fromhex("270100000000000004419a0020")

# Two FLV-style sub-tags whose header fields deliberately disagree with the
# data that follows them, to exercise the aggregate demuxer's bounds checks.
AGGREGATE_BODY = bytes.fromhex(
    "00000000000408000001af0121000000000e000000000006090000012701"
    "0000004100000010")


# -------------------------------------------------------------------- the seeds
SEEDS = {}

# Protocol control messages.
SEEDS["set_chunk_size"] = chunk(2, struct.pack(">I", 4096), type_id=1)
SEEDS["abort"] = chunk(2, struct.pack(">I", 3), type_id=2)
SEEDS["ack"] = chunk(2, struct.pack(">I", 12345), type_id=3)
SEEDS["window_ack_size"] = chunk(2, struct.pack(">I", 2500000), type_id=5)
SEEDS["set_peer_bandwidth"] = chunk(
    2, struct.pack(">I", 2500000) + b"\x02", type_id=6)
SEEDS["user_control"] = user_control(6, 7777) + user_control(0, 1)

# AMF command messages.
SEEDS["connect"] = corrupt(command("connect", 1, CONNECT_ARGS), 0x8c, 0xc3)
SEEDS["create_stream"] = command("createStream", 2, null())
SEEDS["play"] = command("play", 3, null(), string("stream"), num(-2000))
SEEDS["publish"] = command(
    "publish", 4, null(), string("stream"), string("live"))
SEEDS["release_stream"] = (
    command("releaseStream", 5, null(), string("stream"))
    + command("FCPublish", 6, null(), string("stream")))
SEEDS["close_stream"] = (command("closeStream", 7, null())
                         + command("deleteStream", 8, null(), num(1)))
# An AMF3 command: type 17, payload prefixed with one AMF0-encoding marker byte.
SEEDS["command_amf3"] = chunk(
    3, b"\x00" + string("connect") + num(1) + null(), type_id=17, stream_id=1)

# Data and media messages.
SEEDS["metadata"] = corrupt(
    chunk(4, METADATA, type_id=18, stream_id=1), 0x8c, 0xc4)
SEEDS["audio"] = (chunk(5, b"\xaf\x00\x12\x10", type_id=8, stream_id=1)
                  + chunk(5, b"\xaf\x01" + b"\x21\x00" * 8, ts=40,
                          type_id=8, stream_id=1))
SEEDS["video"] = (chunk(6, AVC_SEQ_HEADER, type_id=9, stream_id=1)
                  + chunk(6, AVC_NALU, ts=33, type_id=9, stream_id=1))
SEEDS["aggregate"] = chunk(7, AGGREGATE_BODY, type_id=22, stream_id=1)

# Chunk-layer edge cases.  All four header formats in one stream: a full
# header, then one without a message id, then a timestamp-only header, then a
# continuation that inherits everything from its predecessor.
SEEDS["header_fmts"] = (
    chunk(8, b"\xaf\x01" + b"\x11" * 6, ts=100, type_id=8, stream_id=1)
    + chunk(8, b"\xaf\x01" + b"\x22" * 6, fmt=1, ts=30, type_id=8)
    + chunk(8, b"\xaf\x01" + b"\x33" * 6, fmt=2, ts=30)
    + chunk(8, b"\xaf\x01" + b"\x44" * 6, fmt=3))
# A timestamp of 0xffffff selects the 4-byte extended timestamp that follows
# the chunk header.
SEEDS["extended_timestamp"] = chunk(
    9, b"\x01\x00\x00\x00" + b"\xaf\x01\x21\x00\x21\x00",
    ts=0xffffff, type_id=8, stream_id=1, length=6)
# A 261-byte message, longer than the 128-byte default chunk size, so the
# parser has to reassemble it; two stray bytes break the chunk boundaries.
SEEDS["multi_chunk"] = corrupt(
    corrupt(chunk(10, b"\x27\x01\x00\x00\x00" + bytes(range(256)),
                  type_id=9, stream_id=1), 0x8c, 0xca), 0x10d, 0xca)
# Chunk stream ids that need the 2-byte and 3-byte basic header forms.
SEEDS["large_cs_id"] = (chunk(70, struct.pack(">I", 1), type_id=3)
                        + chunk(400, struct.pack(">I", 2), type_id=3))

# A full publish session, replayed as one stream.
SEEDS["session"] = (SEEDS["set_chunk_size"] + SEEDS["connect"]
                    + SEEDS["create_stream"] + SEEDS["publish"]
                    + SEEDS["metadata"])

# The plain (non-simplified) handshake: C0 + C1 + C2, with no BDMS prefix.
_C1 = b"\x00" * 8 + bytes((i * 7) & 0xff for i in range(1528))
_C2 = bytes((i * 11) & 0xff for i in range(1536))
HANDSHAKE = b"\x03" + _C1 + _C2


def build(name):
    return HANDSHAKE if name == "handshake_c0c1c2" else MAGIC + SEEDS[name]


def main(argv):
    out_dir = argv[1] if len(argv) > 1 else "fuzz_rtmp_seed_corpus"
    os.makedirs(out_dir, exist_ok=True)
    names = sorted(list(SEEDS) + ["handshake_c0c1c2"])
    for name in names:
        with open(os.path.join(out_dir, name + ".rtmp"), "wb") as out:
            out.write(build(name))
    print("Generated %d seeds in %s" % (len(names), out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
