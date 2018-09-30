// brpc - A framework to host and access services throughout Baidu.
// Copyright (c) 2014 Baidu, Inc.

// Date: 2017/04/25 00:23:12

#include <gtest/gtest.h>
#include "brpc/details/hpack.h"
#include "butil/logging.h"

class HPackTest : public testing::Test {
};

// Copied test cases from example of rfc7541
TEST_F(HPackTest, header_with_indexing) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));
    brpc::HPacker::Header h;
    h.name = "Custom-Key";
    h.value = "custom-header";
    brpc::HPackOptions options;
    options.index_policy = brpc::HPACK_INDEX_HEADER;
    butil::IOBufAppender buf;
    p1.Encode(&buf, h, options);
    const ssize_t nwrite = buf.buf().size();
    LOG(INFO) << butil::ToPrintable(buf.buf());
    uint8_t expected[] = {
        0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x6b, 0x65, 0x79,
        0x0d, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d, 0x68, 0x65, 0x61, 0x64,
        0x65, 0x72};
    butil::StringPiece sp((char*)expected, sizeof(expected));
    ASSERT_TRUE(buf.buf().equals(sp));
    brpc::HPacker::Header h2;
    ssize_t nread = p2.Decode(&buf.buf(), &h2);
    ASSERT_EQ(nread, nwrite);
    ASSERT_TRUE(buf.buf().empty());
    std::string lowercase_name = h.name;
    brpc::tolower(&lowercase_name);
    ASSERT_EQ(lowercase_name, h2.name);
    ASSERT_EQ(h.value, h2.value);
}

TEST_F(HPackTest, header_without_indexing) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));
    brpc::HPacker::Header h;
    h.name = ":path";
    h.value = "/sample/path";
    brpc::HPackOptions options;
    options.index_policy = brpc::HPACK_NOT_INDEX_HEADER;
    butil::IOBufAppender buf;
    p1.Encode(&buf, h, options);
    const ssize_t nwrite = buf.buf().size();
    LOG(INFO) << butil::ToPrintable(buf.buf());
    uint8_t expected[] = {
        0x04, 0x0c, 0x2f, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2f, 0x70, 0x61,
        0x74, 0x68, 
    };
    butil::StringPiece sp((char*)expected, sizeof(expected));
    ASSERT_TRUE(buf.buf().equals(sp));
    brpc::HPacker::Header h2;
    ssize_t nread = p2.Decode(&buf.buf(), &h2);
    ASSERT_EQ(nread, nwrite);
    ASSERT_TRUE(buf.buf().empty());
    std::string lowercase_name = h.name;
    brpc::tolower(&lowercase_name);
    ASSERT_EQ(lowercase_name, h2.name);
    ASSERT_EQ(h.value, h2.value);
}

TEST_F(HPackTest, header_never_indexed) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));
    brpc::HPacker::Header h;
    h.name = "password";
    h.value = "secret";
    brpc::HPackOptions options;
    options.index_policy = brpc::HPACK_NEVER_INDEX_HEADER;
    butil::IOBufAppender buf;
    p1.Encode(&buf, h, options);
    const ssize_t nwrite = buf.buf().size();
    LOG(INFO) << butil::ToPrintable(buf.buf());
    uint8_t expected[] = {
        0x10, 0x08, 0x70, 0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64,
        0x06, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 
    };
    butil::StringPiece sp((char*)expected, sizeof(expected));
    ASSERT_TRUE(buf.buf().equals(sp));
    brpc::HPacker::Header h2;
    ssize_t nread = p2.Decode(&buf.buf(), &h2);
    ASSERT_EQ(nread, nwrite);
    ASSERT_TRUE(buf.buf().empty());
    ASSERT_EQ(h.name, h2.name);
    ASSERT_EQ(h.value, h2.value);
}

TEST_F(HPackTest, indexed_header) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));
    brpc::HPacker::Header h;
    h.name = ":method";
    h.value = "GET";
    brpc::HPackOptions options;
    options.index_policy = brpc::HPACK_INDEX_HEADER;
    butil::IOBufAppender buf;
    p1.Encode(&buf, h, options);
    const ssize_t nwrite = buf.buf().size();
    LOG(INFO) << butil::ToPrintable(buf.buf());
    uint8_t expected[] = {
        0x82,
    };
    butil::StringPiece sp((char*)expected, sizeof(expected));
    ASSERT_TRUE(buf.buf().equals(sp));
    brpc::HPacker::Header h2;
    ssize_t nread = p2.Decode(&buf.buf(), &h2);
    ASSERT_EQ(nread, nwrite);
    ASSERT_TRUE(buf.buf().empty());
    ASSERT_EQ(h.name, h2.name);
    ASSERT_EQ(h.value, h2.value);
}

struct ConstHeader {
    const char* name;
    const char* value;
};

TEST_F(HPackTest, requests_without_huffman) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));

    ConstHeader header1[] = { 
        {":method", "GET"},
        {":scheme", "http"},
        {":path", "/"},
        {":authority", "www.example.com"},
    };
    butil::IOBufAppender buf;
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        h.name = header1[i].name;
        h.value = header1[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected1[] = {
        0x82, 0x86, 0x84, 0x41, 0x0f, 0x77, 0x77, 0x77, 0x2e, 0x65, 0x78,
        0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected1, sizeof(expected1))));
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header1[i].name, h.name);
        ASSERT_EQ(header1[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());

    ConstHeader header2[] = { 
        {":method", "GET"},
        {":scheme", "http"},
        {":path", "/"},
        {":authority", "www.example.com"},
        {"cache-control", "no-cache"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        h.name = header2[i].name;
        h.value = header2[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected2[] = {
        0x82, 0x86, 0x84, 0xbe, 0x58, 0x08, 0x6e, 0x6f, 0x2d,
        0x63, 0x61, 0x63, 0x68, 0x65, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected2, sizeof(expected2))));
    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header2[i].name, h.name);
        ASSERT_EQ(header2[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
    ConstHeader header3[] = { 
        {":method", "GET"},
        {":scheme", "https"},
        {":path", "/index.html"},
        {":authority", "www.example.com"},
        {"custom-key", "custom-value"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        h.name = header3[i].name;
        h.value = header3[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected3[] = {
        0x82, 0x87, 0x85, 0xbf, 0x40, 0x0a, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d,
        0x2d, 0x6b, 0x65, 0x79, 0x0c, 0x63, 0x75, 0x73, 0x74, 0x6f, 0x6d, 0x2d,
        0x76, 0x61, 0x6c, 0x75, 0x65, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected3, sizeof(expected3))));
    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header3[i].name, h.name);
        ASSERT_EQ(header3[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
}

TEST_F(HPackTest, requests_with_huffman) {
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(4096));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(4096));

    ConstHeader header1[] = { 
        {":method", "GET"},
        {":scheme", "http"},
        {":path", "/"},
        {":authority", "www.example.com"},
    };
    butil::IOBufAppender buf;
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        h.name = header1[i].name;
        h.value = header1[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        options.encode_name = true;
        options.encode_value = true;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected1[] = {
        0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b,
        0xa0, 0xab, 0x90, 0xf4, 0xff
    };
    LOG(INFO) << butil::ToPrintable(buf.buf());
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected1, sizeof(expected1))));
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header1[i].name, h.name);
        ASSERT_EQ(header1[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());

    ConstHeader header2[] = { 
        {":method", "GET"},
        {":scheme", "http"},
        {":path", "/"},
        {":authority", "www.example.com"},
        {"cache-control", "no-cache"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        h.name = header2[i].name;
        h.value = header2[i].value;
        brpc::HPackOptions options;
        options.encode_name = true;
        options.encode_value = true;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected2[] = {
        0x82, 0x86, 0x84, 0xbe, 0x58, 0x86, 0xa8, 0xeb, 0x10, 0x64, 0x9c, 0xbf, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected2, sizeof(expected2))));
    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header2[i].name, h.name);
        ASSERT_EQ(header2[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
    ConstHeader header3[] = { 
        {":method", "GET"},
        {":scheme", "https"},
        {":path", "/index.html"},
        {":authority", "www.example.com"},
        {"custom-key", "custom-value"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        h.name = header3[i].name;
        h.value = header3[i].value;
        brpc::HPackOptions options;
        options.encode_name = true;
        options.encode_value = true;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected3[] = {
        0x82, 0x87, 0x85, 0xbf, 0x40, 0x88, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xa9,
        0x7d, 0x7f, 0x89, 0x25, 0xa8, 0x49, 0xe9, 0x5b, 0xb8, 0xe8, 0xb4, 0xbf, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected3, sizeof(expected3))));
    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header3[i].name, h.name);
        ASSERT_EQ(header3[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
}

TEST_F(HPackTest, responses_without_huffman) {
    // https://tools.ietf.org/html/rfc7541#appendix-C.5
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(256));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(256));

    ConstHeader header1[] = { 
        {":status", "302"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
        {"location", "https://www.example.com"},
    };
    butil::IOBufAppender buf;
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        h.name = header1[i].name;
        h.value = header1[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected1[] = {
        0x48, 0x03, 0x33, 0x30, 0x32, 0x58, 0x07, 0x70, 0x72, 0x69, 0x76, 0x61,
        0x74, 0x65, 0x61, 0x1d, 0x4d, 0x6f, 0x6e, 0x2c, 0x20, 0x32, 0x31, 0x20,
        0x4f, 0x63, 0x74, 0x20, 0x32, 0x30, 0x31, 0x33, 0x20, 0x32, 0x30, 0x3a,
        0x31, 0x33, 0x3a, 0x32, 0x31, 0x20, 0x47, 0x4d, 0x54, 0x6e, 0x17, 0x68, 
        0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x77, 0x77, 0x77, 0x2e, 0x65,
        0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 
    };
    LOG(INFO) << butil::ToPrintable(buf.buf());
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected1, sizeof(expected1))));
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header1[i].name, h.name);
        ASSERT_EQ(header1[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());

    ConstHeader header2[] = { 
        {":status", "307"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
        {"location", "https://www.example.com"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        h.name = header2[i].name;
        h.value = header2[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected2[] = {
        0x48, 0x03, 0x33, 0x30, 0x37, 0xc1, 0xc0, 0xbf, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected2, sizeof(expected2))));
    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header2[i].name, h.name);
        ASSERT_EQ(header2[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
    ConstHeader header3[] = { 
        {":status", "200"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:22 GMT"},
        {"location", "https://www.example.com"},
        {"content-encoding", "gzip"},
        {"set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        h.name = header3[i].name;
        h.value = header3[i].value;
        brpc::HPackOptions options;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected3[] = {
        0x88, 0xc1, 0x61, 0x1d, 0x4d, 0x6f, 0x6e, 0x2c, 0x20, 0x32, 0x31, 0x20,
        0x4f, 0x63, 0x74, 0x20, 0x32, 0x30, 0x31, 0x33, 0x20, 0x32, 0x30, 0x3a,
        0x31, 0x33, 0x3a, 0x32, 0x32, 0x20, 0x47, 0x4d, 0x54, 0xc0, 0x5a, 0x04,
        0x67, 0x7a, 0x69, 0x70, 0x77, 0x38, 0x66, 0x6f, 0x6f, 0x3d, 0x41, 0x53, 
        0x44, 0x4a, 0x4b, 0x48, 0x51, 0x4b, 0x42, 0x5a, 0x58, 0x4f, 0x51, 0x57,
        0x45, 0x4f, 0x50, 0x49, 0x55, 0x41, 0x58, 0x51, 0x57, 0x45, 0x4f, 0x49,
        0x55, 0x3b, 0x20, 0x6d, 0x61, 0x78, 0x2d, 0x61, 0x67, 0x65, 0x3d, 0x33,
        0x36, 0x30, 0x30, 0x3b, 0x20, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 
        0x3d, 0x31, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected3, sizeof(expected3))));
    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header3[i].name, h.name);
        ASSERT_EQ(header3[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
}

TEST_F(HPackTest, responses_with_huffman) {
    // https://tools.ietf.org/html/rfc7541#appendix-C.5
    brpc::HPacker p1;
    ASSERT_EQ(0, p1.Init(256));
    brpc::HPacker p2;
    ASSERT_EQ(0, p2.Init(256));

    ConstHeader header1[] = { 
        {":status", "302"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
        {"location", "https://www.example.com"},
    };
    butil::IOBufAppender buf;
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        h.name = header1[i].name;
        h.value = header1[i].value;
        brpc::HPackOptions options;
        options.encode_name = true;
        options.encode_value = true;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected1[] = {
        0x48, 0x82, 0x64, 0x02, 0x58, 0x85, 0xae, 0xc3, 0x77, 0x1a, 0x4b, 0x61,
        0x96, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44, 0xa8, 0x20, 0x05,
        0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x82, 0xa6, 0x2d, 0x1b, 0xff, 0x6e,
        0x91, 0x9d, 0x29, 0xad, 0x17, 0x18, 0x63, 0xc7, 0x8f, 0x0b, 0x97, 0xc8, 
        0xe9, 0xae, 0x82, 0xae, 0x43, 0xd3,             
    };
    LOG(INFO) << butil::ToPrintable(buf.buf());
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected1, sizeof(expected1))));
    for (size_t i = 0; i < ARRAY_SIZE(header1); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header1[i].name, h.name);
        ASSERT_EQ(header1[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());

    ConstHeader header2[] = { 
        {":status", "307"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:21 GMT"},
        {"location", "https://www.example.com"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        h.name = header2[i].name;
        h.value = header2[i].value;
        brpc::HPackOptions options;
        options.encode_name = true;
        options.encode_value = true;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected2[] = {
        0x48, 0x83, 0x64, 0x0e, 0xff, 0xc1, 0xc0, 0xbf, 
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected2, sizeof(expected2))));
    for (size_t i = 0; i < ARRAY_SIZE(header2); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header2[i].name, h.name);
        ASSERT_EQ(header2[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
    ConstHeader header3[] = { 
        {":status", "200"},
        {"cache-control", "private"},
        {"date", "Mon, 21 Oct 2013 20:13:22 GMT"},
        {"location", "https://www.example.com"},
        {"content-encoding", "gzip"},
        {"set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        h.name = header3[i].name;
        h.value = header3[i].value;
        brpc::HPackOptions options;
        options.encode_name = true;
        options.encode_value = true;
        options.index_policy = brpc::HPACK_INDEX_HEADER;
        p1.Encode(&buf, h, options);
    }
    uint8_t expected3[] = {
        0x88, 0xc1, 0x61, 0x96, 0xd0, 0x7a, 0xbe, 0x94, 0x10, 0x54, 0xd4, 0x44,
        0xa8, 0x20, 0x05, 0x95, 0x04, 0x0b, 0x81, 0x66, 0xe0, 0x84, 0xa6, 0x2d,
        0x1b, 0xff, 0xc0, 0x5a, 0x83, 0x9b, 0xd9, 0xab, 0x77, 0xad, 0x94, 0xe7,
        0x82, 0x1d, 0xd7, 0xf2, 0xe6, 0xc7, 0xb3, 0x35, 0xdf, 0xdf, 0xcd, 0x5b, 
        0x39, 0x60, 0xd5, 0xaf, 0x27, 0x08, 0x7f, 0x36, 0x72, 0xc1, 0xab, 0x27,
        0x0f, 0xb5, 0x29, 0x1f, 0x95, 0x87, 0x31, 0x60, 0x65, 0xc0, 0x03, 0xed,
        0x4e, 0xe5, 0xb1, 0x06, 0x3d, 0x50, 0x07,
    };
    ASSERT_TRUE(buf.buf().equals(butil::StringPiece((char*)expected3, sizeof(expected3))));
    for (size_t i = 0; i < ARRAY_SIZE(header3); ++i) {
        brpc::HPacker::Header h;
        ASSERT_GT(p2.Decode(&buf.buf(), &h), 0);
        ASSERT_EQ(header3[i].name, h.name);
        ASSERT_EQ(header3[i].value, h.value);
    }
    ASSERT_TRUE(buf.buf().empty());
}
