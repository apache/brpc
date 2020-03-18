// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <gtest/gtest.h>
#include "butil/string_splitter.h"
#include <stdlib.h>

namespace {
class StringSplitterTest : public ::testing::Test{
protected:
    StringSplitterTest(){};
    virtual ~StringSplitterTest(){};
    virtual void SetUp() {
        srand (time(0));
    };
    virtual void TearDown() {
    };
};


TEST_F(StringSplitterTest, sanity) {
    const char* str = "hello there!   man ";
    butil::StringSplitter ss(str, ' ');
    // "hello"
    ASSERT_TRUE(ss != NULL);
    ASSERT_EQ(5ul, ss.length());
    ASSERT_EQ(ss.field(), str);

    // "there!"
    ++ss;
    ASSERT_NE(ss, (void*)NULL);
    ASSERT_EQ(6ul, ss.length());
    ASSERT_EQ(ss.field(), str+6);

    // "man"
    ++ss;
    ASSERT_TRUE(ss);
    ASSERT_EQ(3ul, ss.length());
    ASSERT_EQ(ss.field(), str+15);

    ++ss;
    ASSERT_FALSE(ss);
    ASSERT_EQ(0ul, ss.length());
    ASSERT_EQ(ss.field(), str + strlen(str));

    // consecutive separators are treated as zero-length field inside
    butil::StringSplitter ss2(str, ' ', butil::ALLOW_EMPTY_FIELD);

    // "hello"
    ASSERT_TRUE(ss2);
    ASSERT_EQ(5ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "hello", ss2.length()));

    // "there!"
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(6ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "there!", ss2.length()));

    // ""
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str+13);

    // ""
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str+14);
    
    // "man"
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(3ul, ss2.length());
    ASSERT_EQ(ss2.field(), str+15);

    ++ss2;
    ASSERT_FALSE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str+19);
}


TEST_F(StringSplitterTest, single_word)
{
    const char* str = "apple";
    butil::StringSplitter ss(str, ' ');
    // "apple"
    ASSERT_TRUE(ss);
    ASSERT_EQ(5ul, ss.length());
    ASSERT_EQ(ss.field(), str);

    ++ss;
    ASSERT_FALSE(ss);
    ASSERT_EQ(0ul, ss.length());
    ASSERT_EQ(ss.field(), str+5);
}

TEST_F(StringSplitterTest, starting_with_separator) {
    const char* str = "  apple";
    butil::StringSplitter ss(str, ' ');
    // "apple"
    ASSERT_TRUE(ss);
    ASSERT_EQ(5ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "apple", ss.length()));

    ++ss;
    ASSERT_FALSE(ss);
    ASSERT_EQ(0ul, ss.length());
    ASSERT_EQ(ss.field(), str + strlen(str));

    butil::StringSplitter ss2(str, ' ', butil::ALLOW_EMPTY_FIELD);
    // ""
    ASSERT_TRUE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str);

    // ""
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str+1);
    
    // "apple"
    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(5ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "apple", ss2.length()));

    ++ss2;
    ASSERT_FALSE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str + strlen(str));
}

TEST_F(StringSplitterTest, site_id_as_example) {
    const char* str = "|123|12||1|21|4321";
    butil::StringSplitter ss(str, '|');
    ASSERT_TRUE(ss);
    ASSERT_EQ(3ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "123", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(2ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "12", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(1ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "1", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(2ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "21", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(4ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "4321", ss.length()));

    ++ss;
    ASSERT_FALSE(ss);
    ASSERT_EQ(0ul, ss.length());
    ASSERT_EQ(ss.field(), str + strlen(str));
}

TEST_F(StringSplitterTest, number_list) {
    const char* str = " 123,,12,1,  21 4321\00056";
    butil::StringMultiSplitter ss(str, ", ");
    ASSERT_TRUE(ss);
    ASSERT_EQ(3ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "123", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(2ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "12", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(1ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "1", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(2ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "21", ss.length()));

    ss++;
    ASSERT_TRUE(ss);
    ASSERT_EQ(4ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "4321", ss.length()));

    ++ss;
    ASSERT_FALSE(ss);
    ASSERT_EQ(0ul, ss.length());
    ASSERT_EQ(ss.field(), str + strlen(str));

    // contains embedded '\0'
    const size_t str_len = 23;
    butil::StringMultiSplitter ss2(str, str + str_len, ", ");
    ASSERT_TRUE(ss2);
    ASSERT_EQ(3ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "123", ss2.length()));

    ss2++;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(2ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "12", ss2.length()));

    ss2++;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(1ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "1", ss2.length()));

    ss2++;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(2ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "21", ss2.length()));

    ss2++;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(7ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "4321\00056", ss2.length()));

    ++ss2;
    ASSERT_FALSE(ss2);
    ASSERT_EQ(0ul, ss2.length());
    ASSERT_EQ(ss2.field(), str + str_len);
}

TEST_F(StringSplitterTest, cast_type) {
    const char* str = "-1\t123\t111\t1\t10\t11\t1.3\t3.1415926\t127\t128\t256";
    int i = 0;
    unsigned int u = 0;
    long l = 0;
    unsigned long ul = 0;
    long long ll = 0;
    unsigned long long ull = 0;
    float f = 0.0;
    double d = 0.0;
    
    butil::StringSplitter ss(str, '\t');
    ASSERT_TRUE(ss);

    ASSERT_EQ(0, ss.to_int(&i));
    ASSERT_EQ(-1, i);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_uint(&u));
    ASSERT_EQ(123u, u);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_long(&l));
    ASSERT_EQ(111, l);


    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_ulong(&ul));
    ASSERT_EQ(1ul, ul);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_longlong(&ll));
    ASSERT_EQ(10, ll);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_ulonglong(&ull));
    ASSERT_EQ(11ull, ull);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_float(&f));
    ASSERT_FLOAT_EQ(1.3, f);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(0, ss.to_double(&d));
    ASSERT_DOUBLE_EQ(3.1415926, d);

    ASSERT_TRUE(++ss);
    int8_t c = 0;
    ASSERT_EQ(0, ss.to_int8(&c));
    ASSERT_EQ(127, c);

    ASSERT_TRUE(++ss);
    uint8_t uc = 0;
    ASSERT_EQ(0, ss.to_uint8(&uc));
    ASSERT_EQ(128U, uc);

    ASSERT_TRUE(++ss);
    ASSERT_EQ(-1, ss.to_uint8(&uc));
}

TEST_F(StringSplitterTest, split_limit_len) {
    const char* str = "1\t1\0003\t111\t1\t10\t11\t1.3\t3.1415926";
    butil::StringSplitter ss(str, str + 5, '\t');

    ASSERT_TRUE(ss);
    ASSERT_EQ(1ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "1", ss.length()));

    ++ss;
    ASSERT_TRUE(ss);
    ASSERT_EQ(3ul, ss.length());
    ASSERT_FALSE(strncmp(ss.field(), "1\0003", ss.length()));

    ++ss;
    ASSERT_FALSE(ss);

    // Allows using '\0' as separator
    butil::StringSplitter ss2(str, str + 5, '\0');

    ASSERT_TRUE(ss2);
    ASSERT_EQ(3ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "1\t1", ss2.length()));

    ++ss2;
    ASSERT_TRUE(ss2);
    ASSERT_EQ(1ul, ss2.length());
    ASSERT_FALSE(strncmp(ss2.field(), "3", ss2.length()));

    ++ss2;
    ASSERT_FALSE(ss2);

    butil::StringPiece sp(str, 5);
    // Allows using '\0' as separator
    butil::StringSplitter ss3(sp, '\0');

    ASSERT_TRUE(ss3);
    ASSERT_EQ(3ul, ss3.length());
    ASSERT_FALSE(strncmp(ss3.field(), "1\t1", ss3.length()));

    ++ss3;
    ASSERT_TRUE(ss3);
    ASSERT_EQ(1ul, ss3.length());
    ASSERT_FALSE(strncmp(ss3.field(), "3", ss3.length()));

    ++ss3;
    ASSERT_FALSE(ss3);
}

TEST_F(StringSplitterTest, key_value_pairs_splitter_sanity) {
    std::string kvstr = "key1=value1&&&key2=value2&key3=value3&===&key4=&=&=value5";
    for (int i = 0 ; i < 3; ++i) {
        // Test three constructors
        butil::KeyValuePairsSplitter* psplitter = NULL;
        if (i == 0) {
            psplitter = new butil::KeyValuePairsSplitter(kvstr, '&', '=');
        } else if (i == 1) {
            psplitter = new butil::KeyValuePairsSplitter(
                    kvstr.data(), kvstr.data() + kvstr.size(), '&', '=');
        } else if (i == 2) {
            psplitter = new butil::KeyValuePairsSplitter(kvstr.c_str(), '&', '=');
        }
        butil::KeyValuePairsSplitter& splitter = *psplitter;

        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "key1");
        ASSERT_EQ(splitter.value(), "value1");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "key2");
        ASSERT_EQ(splitter.value(), "value2");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "key3");
        ASSERT_EQ(splitter.value(), "value3");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "");
        ASSERT_EQ(splitter.value(), "==");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "key4");
        ASSERT_EQ(splitter.value(), "");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "");
        ASSERT_EQ(splitter.value(), "");
        ++splitter;
        ASSERT_TRUE(splitter);
        ASSERT_EQ(splitter.key(), "");
        ASSERT_EQ(splitter.value(), "value5");
        ++splitter;
        ASSERT_FALSE(splitter);

        delete psplitter;
    }
}

}
