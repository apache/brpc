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

// brpc - A framework to host and access services throughout Baidu.

// Date: 2015/01/20 19:01:06

#include <gtest/gtest.h>
#include "butil/gperftools_profiler.h"
#include "butil/third_party/snappy/snappy.h"
#include "butil/macros.h"
#include "butil/iobuf.h"
#include "butil/time.h"
#include "snappy_message.pb.h"
#include "brpc/policy/snappy_compress.h"
#include "brpc/policy/gzip_compress.h"

typedef bool (*Compress)(const google::protobuf::Message&, butil::IOBuf*);
typedef bool (*Decompress)(const butil::IOBuf&, google::protobuf::Message*);

inline void CompressMessage(const char* method_name,
                            int num, snappy_message::SnappyMessageProto& msg, 
                            int len, Compress compress, Decompress decompress) {
    butil::Timer timer;
    size_t compression_length = 0;
    int64_t total_compress_time = 0;
    int64_t total_decompress_time = 0;
    snappy_message::SnappyMessageProto new_msg;
    for (int index = 0; index < num; index++) {
        butil::IOBuf buf;
        timer.start();
        ASSERT_TRUE(compress(msg, &buf));
        timer.stop();
        total_compress_time += timer.n_elapsed();
        compression_length += buf.length();
        timer.start();
        ASSERT_TRUE(decompress(buf, &new_msg));
        timer.stop();
        total_decompress_time += timer.n_elapsed();
    }
    float compression_ratio = compression_length / (((double)num) * len);
    printf("%20s%20d%20f%20f%30f%30f%29f%%\n", method_name, len, 
            total_compress_time/1000.0/num, total_decompress_time/1000.0/num, 
            1000000000.0/1024/1024*num*len/total_compress_time,
            1000000000.0/1024/1024*num*len/total_decompress_time,
            compression_ratio*100.0);
}

static bool SnappyDecompressIOBuf(char* input, size_t len, butil::IOBuf* buf) {
    size_t decompress_length;
    if (!butil::snappy::GetUncompressedLength(input, len, &decompress_length)) {
        return false;
    }
    char* output = new char[decompress_length];
    if (!butil::snappy::RawUncompress(input, len, output)) {
        delete [] output;
        return false;
    }
    buf->append(output, decompress_length);
    delete [] output;
    return true;
}

class test_compress_method : public testing::Test {};

TEST_F(test_compress_method, snappy) {
    snappy_message::SnappyMessageProto old_msg;
    old_msg.set_text("Hello World!");
    old_msg.add_numbers(2);
    old_msg.add_numbers(7);
    old_msg.add_numbers(45);
    butil::IOBuf buf;
    ASSERT_TRUE(brpc::policy::SnappyCompress(old_msg, &buf));
    snappy_message::SnappyMessageProto new_msg;
    ASSERT_TRUE(brpc::policy::SnappyDecompress(buf, &new_msg));
    ASSERT_TRUE(strcmp(new_msg.text().c_str(), "Hello World!") == 0);
    ASSERT_TRUE(new_msg.numbers_size() == 3);
    ASSERT_EQ(new_msg.numbers(0), 2);
    ASSERT_EQ(new_msg.numbers(1), 7);
    ASSERT_EQ(new_msg.numbers(2), 45);
}

TEST_F(test_compress_method, snappy_iobuf) {
    butil::IOBuf buf, output_buf, check_buf; 
    const char* test = "this is a test";
    buf.append(test, strlen(test));
    ASSERT_TRUE(brpc::policy::SnappyCompress(buf, &output_buf)); 
    ASSERT_TRUE(brpc::policy::SnappyDecompress(output_buf, &check_buf));
    ASSERT_STREQ(check_buf.to_string().c_str(), test);
}

TEST_F(test_compress_method, mass_snappy) {
    snappy_message::SnappyMessageProto old_msg;
    int len = 12435; 
    char* text = new char[len + 1];
    for (int j = 0; j < len;) {
        for (int i = 0; i < 26 && j < len; i++) {
            text[j++] = 'a' + i;
        }
        for (int i = 0; i < 10 && j < len; i++) {
            text[j++] = '0' + i;
        }
    }
    text[len] = '\0';
    old_msg.set_text(text);
    old_msg.add_numbers(2);
    old_msg.add_numbers(7);
    old_msg.add_numbers(45);
    butil::IOBuf buf;
    ProfilerStart("./snappy_compress.prof");
    ASSERT_TRUE(brpc::policy::SnappyCompress(old_msg, &buf));
    snappy_message::SnappyMessageProto new_msg;
    ASSERT_TRUE(brpc::policy::SnappyDecompress(buf, &new_msg));
    ProfilerStop();
    ASSERT_TRUE(strcmp(new_msg.text().c_str(), text) == 0);
    ASSERT_TRUE(new_msg.numbers_size() == 3);
    ASSERT_EQ(new_msg.numbers(0), 2);
    ASSERT_EQ(new_msg.numbers(1), 7);
    ASSERT_EQ(new_msg.numbers(2), 45);
    delete [] text;
}

TEST_F(test_compress_method, snappy_test) {
    int len = 200;
    char* text = new char[len + 1];
    for (int j = 0; j < len;) {
        for (int i = 0; i < 26 && j < len; i++) {
            text[j++] = 'a' + i;
        }
        for (int i = 0; i < 10 && j < len; i++) {
            text[j++] = '0' + i;
        }
    }
    text[len] = '\0';
    butil::IOBuf buf;
    std::string output;
    std::string append_string;
    ASSERT_TRUE(butil::snappy::Compress(text, len, &output));
    size_t com_len1 = output.size();
    const char* s_text = "123456";
    ASSERT_TRUE(butil::snappy::Compress(s_text, strlen(s_text), &append_string));
    output.append(append_string);
    std::string uncompress_str;
    std::string uncompress_str_t;
    char* ptr = const_cast<char*>(output.c_str());
    ASSERT_TRUE(butil::snappy::Uncompress(ptr, com_len1, &uncompress_str));
    ptr = const_cast<char*>(append_string.c_str());
    ASSERT_TRUE(butil::snappy::Uncompress(ptr, strlen(ptr), &uncompress_str_t));
    delete [] text;
}

TEST_F(test_compress_method, throughput_compare) {
    int len = 0;
    int len_subs[] = {128, 1024, 16*1024, 32*1024, 512*1024}; 
    butil::Timer timer;
    printf("%20s%20s%20s%20s%30s%30s%30s\n", "Compress method", "Compress size(B)", 
           "Compress time(us)", "Decompress time(us)", "Compress throughput(MB/s)", 
           "Decompress throughput(MB/s)", "Compress ratio");    
    for (size_t num = 0; num < ARRAY_SIZE(len_subs); ++num) {
        len = len_subs[num];
        snappy_message::SnappyMessageProto old_msg;
        char* text = new char[len + 1];
        for (int j = 0; j < len;) {
            for (int i = 0; i < 26 && j < len; i++) {
                text[j++] = 'a' + i;
            }
            for (int i = 0; i < 10 && j < len; i++) {
                text[j++] = '0' + i;
            }
        }
        text[len] = '\0';
        old_msg.set_text(text);
        int k = std::min(32*1024*1024/len, 5000);
        CompressMessage("Snappy", k, old_msg, len, 
                         brpc::policy::SnappyCompress, 
                         brpc::policy::SnappyDecompress);
        CompressMessage("Gzip", k, old_msg, len, 
                         brpc::policy::GzipCompress, 
                         brpc::policy::GzipDecompress);
        CompressMessage("Zlib", k, old_msg, len, 
                         brpc::policy::ZlibCompress, 
                         brpc::policy::ZlibDecompress);
        printf("\n");
        delete [] text;
    }
}

TEST_F(test_compress_method, throughput_compare_complete_random) {
    char str_table[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    int rand_num = 0;
    int len = 0;
    int len_subs[] = {128, 1024, 16*1024, 32*1024, 512 * 1024}; 
    butil::Timer timer;
    printf("%20s%20s%20s%20s%30s%30s%30s\n", "Compress method", "Compress size(B)", 
           "Compress time(us)", "Decompress time(us)", "Compress throughput(MB/s)", 
           "Decompress throughput(MB/s)", "Compress ratio");
    for (size_t num = 0; num < ARRAY_SIZE(len_subs); ++num) {
        len = len_subs[num];
        snappy_message::SnappyMessageProto old_msg;
        char* text = new char[len + 1];
        for (int j = 0; j < len;) {
            rand_num = rand()%62;
            text[j++] = str_table[rand_num];
        }
        text[len] = '\0';
        old_msg.set_text(text);
        int k = std::min(32*1024*1024/len, 5000);
        CompressMessage("Snappy", k, old_msg, len, 
                         brpc::policy::SnappyCompress, 
                         brpc::policy::SnappyDecompress);
        CompressMessage("Gzip", k, old_msg, len, 
                         brpc::policy::GzipCompress, 
                         brpc::policy::GzipDecompress);
        CompressMessage("Zlib", k, old_msg, len, 
                         brpc::policy::ZlibCompress, 
                         brpc::policy::ZlibDecompress);
        printf("\n");
        delete [] text;
    }
}

TEST_F(test_compress_method, mass_snappy_iobuf) {
    butil::IOBuf buf; 
    int len = 782;
    char* text = new char[len + 1];
    for (int j = 0; j < len;) {
        for (int i = 0; i < 26 && j < len; i++) {
            text[j++] = 'a' + i;
        }
    }
    text[len] = '\0';
    buf.append(text, strlen(text));
    butil::IOBuf output_buf, check_buf;
    ASSERT_TRUE(brpc::policy::SnappyCompress(buf, &output_buf)); 
    const std::string output_str = output_buf.to_string();
    len = output_str.size();
    ASSERT_TRUE(SnappyDecompressIOBuf(const_cast<char*>(output_str.data()), len, &check_buf));
    std::string check_str = check_buf.to_string();
    ASSERT_TRUE(strcmp(check_str.c_str(), text) == 0);
    delete [] text;
}
