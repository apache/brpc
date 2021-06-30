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

#include <sys/time.h>
#include <gtest/gtest.h>
#include <iostream>
#include <fstream>
#include <string>
#include <google/protobuf/text_format.h>
#include "butil/iobuf.h"
#include "butil/third_party/rapidjson/rapidjson.h"
#include "butil/time.h"
#include "butil/gperftools_profiler.h"
#include "json2pb/pb_to_json.h"
#include "json2pb/json_to_pb.h"
#include "json2pb/encode_decode.h"
#include "message.pb.h"
#include "addressbook1.pb.h"
#include "addressbook.pb.h"
#include "addressbook_encode_decode.pb.h"
#include "addressbook_map.pb.h"

namespace {  // just for coding-style check

using addressbook::AddressBook;
using addressbook::Person;

class ProtobufJsonTest : public testing::Test {
protected:
    void SetUp() {}
    void TearDown() {}
};

inline int64_t gettimeofday_us() {
    timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000000L + now.tv_usec;
}

TEST_F(ProtobufJsonTest, json_to_pb_normal_case) {
    const int N = 1000;
    int64_t total_tm = 0;
    int64_t total_tm2 = 0;
    for (int i = 0; i < N; ++i) {
        std::string info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\"ext\":"
                            "{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},"
                            "\"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,"
                            "\"ext\":{\"age\":1666666660, \"databyte\":\"d2VsY29tZQ==\","
                            "\"enumtype\":2},\"uid\":\"someone0\"}], \"judge\":false,"
                            "\"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
        std::string error; 
  
        JsonContextBody data;
        const int64_t tm1 = gettimeofday_us();
        const bool ret = json2pb::JsonToProtoMessage(info3, &data, &error);
        const int64_t tm2 = gettimeofday_us();
        total_tm += tm2 - tm1;
        ASSERT_TRUE(ret);
    
        std::string info4;  
        std::string error1;
        const int64_t tm3 = gettimeofday_us();
        bool ret2 = json2pb::ProtoMessageToJson(data, &info4, &error1);
        const int64_t tm4 = gettimeofday_us();
        ASSERT_TRUE(ret2);
        total_tm2 += tm4 - tm3;
#ifndef RAPIDJSON_VERSION_0_1
        ASSERT_STREQ("{\"data\":[1,2,3,4,5,6,7,8,9,10],"
                     "\"judge\":false,\"spur\":2.0,\"content\":[{\"uid\":\"someone\","
                     "\"distance\":1.0,\"ext\":{\"age\":1666666666,\"databyte\":\"d2VsY29tZQ==\","
                     "\"enumtype\":\"HOME\"}},\{\"uid\":\"someone0\",\"distance\":10.0,\"ext\":"
                     "{\"age\":1666666660,\"databyte\":\"d2VsY29tZQ==\",\"enumtype\":\"WORK\"}}]}", 
                      info4.data());
#else
        ASSERT_STREQ("{\"data\":[1,2,3,4,5,6,7,8,9,10],"
                     "\"judge\":false,\"spur\":2,\"content\":[{\"uid\":\"someone\","
                     "\"distance\":1,\"ext\":{\"age\":1666666666,\"databyte\":\"d2VsY29tZQ==\","
                     "\"enumtype\":\"HOME\"}},\{\"uid\":\"someone0\",\"distance\":10,\"ext\":"
                     "{\"age\":1666666660,\"databyte\":\"d2VsY29tZQ==\",\"enumtype\":\"WORK\"}}]}", 
                     info4.data());
#endif
    }
    std::cout << "json2pb=" << total_tm / N
              << "us pb2json=" << total_tm2 / N << "us"
              << std::endl;
}

TEST_F(ProtobufJsonTest, json_base64_string_to_pb_types_case) {
    std::string info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\"ext\":"
                        "{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},"
                        "\"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,"
                        "\"ext\":{\"age\":1666666660, \"databyte\":\"d2VsY29tZTA=\","
                        "\"enumtype\":2},\"uid\":\"someone0\"}], \"judge\":false,"
                        "\"spur\":2}";  
    std::string error; 

    JsonContextBody data;
    json2pb::Json2PbOptions options_j2pb;
    options_j2pb.base64_to_bytes = true;
    const bool ret = json2pb::JsonToProtoMessage(info3, &data, options_j2pb, &error);
    ASSERT_TRUE(ret);
    ASSERT_TRUE(data.content_size() == 2);
    ASSERT_EQ(data.content(0).ext().databyte(), "welcome");
    ASSERT_EQ(data.content(1).ext().databyte(), "welcome0");

    std::string info4;  
    std::string error1;
    json2pb::Pb2JsonOptions options_pb2j;
    options_pb2j.bytes_to_base64 = true;
    bool ret2 = json2pb::ProtoMessageToJson(data, &info4, options_pb2j, &error1);
    ASSERT_TRUE(ret2);
#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"judge\":false,\"spur\":2.0,\"content\":[{\"uid\":\"someone\","
                 "\"distance\":1.0,\"ext\":{\"age\":1666666666,\"databyte\":\"d2VsY29tZQ==\","
                 "\"enumtype\":\"HOME\"}},\{\"uid\":\"someone0\",\"distance\":10.0,\"ext\":"
                 "{\"age\":1666666660,\"databyte\":\"d2VsY29tZTA=\",\"enumtype\":\"WORK\"}}]}", 
                  info4.data());
#else
    ASSERT_STREQ("{\"judge\":false,\"spur\":2,\"content\":[{\"uid\":\"someone\","
                 "\"distance\":1,\"ext\":{\"age\":1666666666,\"databyte\":\"d2VsY29tZQ==\","
                 "\"enumtype\":\"HOME\"}},\{\"uid\":\"someone0\",\"distance\":10,\"ext\":"
                 "{\"age\":1666666660,\"databyte\":\"d2VsY29tZTA=\",\"enumtype\":\"WORK\"}}]}", 
                 info4.data());
#endif
}

TEST_F(ProtobufJsonTest, json_to_pb_map_case) {
    std::string json = "{\"addr\":\"baidu.com\","
                       "\"numbers\":{\"tel\":123456,\"cell\":654321},"
                       "\"contacts\":{\"email\":\"frank@baidu.com\","
                       "               \"office\":\"Shanghai\"},"
                       "\"friends\":{\"John\":[{\"school\":\"SJTU\",\"year\":2007}]}}";
    std::string error;
    AddressNoMap ab1;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab1, &error));
    ASSERT_EQ("baidu.com", ab1.addr());

    AddressIntMap ab2;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab2, &error));
    ASSERT_EQ("baidu.com", ab2.addr());
    ASSERT_EQ("tel", ab2.numbers(0).key());
    ASSERT_EQ(123456, ab2.numbers(0).value());
    ASSERT_EQ("cell", ab2.numbers(1).key());
    ASSERT_EQ(654321, ab2.numbers(1).value());
    
    AddressStringMap ab3;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab3, &error));
    ASSERT_EQ("baidu.com", ab3.addr());
    ASSERT_EQ("email", ab3.contacts(0).key());
    ASSERT_EQ("frank@baidu.com", ab3.contacts(0).value());
    ASSERT_EQ("office", ab3.contacts(1).key());
    ASSERT_EQ("Shanghai", ab3.contacts(1).value());

    AddressComplex ab4;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab4, &error)) << error;
    ASSERT_EQ("baidu.com", ab4.addr());
    ASSERT_EQ("John", ab4.friends(0).key());
    ASSERT_EQ("SJTU", ab4.friends(0).value(0).school());
    ASSERT_EQ(2007, ab4.friends(0).value(0).year());

    std::string old_json = "{\"addr\":\"baidu.com\","
                           "\"numbers\":[{\"key\":\"tel\",\"value\":123456},"
                           "             {\"key\":\"cell\",\"value\":654321}]}";
    ab2.Clear();
    ASSERT_TRUE(json2pb::JsonToProtoMessage(old_json, &ab2, &error)) << error;
    ASSERT_EQ("baidu.com", ab2.addr());
    ASSERT_EQ("tel", ab2.numbers(0).key());
    ASSERT_EQ(123456, ab2.numbers(0).value());
    ASSERT_EQ("cell", ab2.numbers(1).key());
    ASSERT_EQ(654321, ab2.numbers(1).value());
}

TEST_F(ProtobufJsonTest, json_to_pb_encode_decode) {
    std::string info3 = "{\"@Content_Test%@\":[{\"Distance_info_\":1,\
                         \"_ext%T_\":{\"Aa_ge(\":1666666666, \"databyte(std::string)\":\
                         \"d2VsY29tZQ==\", \"enum--type\":\"HOME\"},\"uid*\":\"welcome\"}], \
                         \"judge\":false, \"spur\":2, \"data:array\":[]}";  
    printf("----------test json to pb------------\n\n");
    std::string error; 
    JsonContextBodyEncDec data; 
    ASSERT_TRUE(json2pb::JsonToProtoMessage(info3, &data, &error));

    std::string info4;  
    std::string error1;
    ASSERT_TRUE(json2pb::ProtoMessageToJson(data, &info4, &error1));
#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"judge\":false,\"spur\":2.0,"
                 "\"@Content_Test%@\":[{\"uid*\":\"welcome\",\"Distance_info_\":1.0,"
                 "\"_ext%T_\":{\"Aa_ge(\":1666666666,\"databyte(std::string)\":\"d2VsY29tZQ==\","
                 "\"enum--type\":\"HOME\"}}]}", info4.data());
#else
    ASSERT_STREQ("{\"judge\":false,\"spur\":2,"
                 "\"@Content_Test%@\":[{\"uid*\":\"welcome\",\"Distance_info_\":1,"
                 "\"_ext%T_\":{\"Aa_ge(\":1666666666,\"databyte(std::string)\":\"d2VsY29tZQ==\","
                 "\"enum--type\":\"HOME\"}}]}", info4.data());
#endif
}

TEST_F(ProtobufJsonTest, json_to_pb_unicode_case) {
    AddressBook address_book;

    Person* person = address_book.add_person();
 
    person->set_id(100);
    
    char name[255 * 1024];
    for (int j = 0; j < 255; j++) {
        for (int i = 0; i < 1024; i++) {
            name[j*1024 + i] = i + 1;
        }
    }
    name[255 * 1024 - 1] = '\0';
    person->set_name(name);
    person->set_data(-240000000);
    person->set_data32(6);
    person->set_data64(-1820000000);
    person->set_datadouble(123.456);
    person->set_datadouble_scientific(1.23456789e+08);
    person->set_datafloat_scientific(1.23456789e+08);
    person->set_datafloat(8.6123);
    person->set_datau32(60);
    person->set_datau64(960);
    person->set_databool(0);
    person->set_databyte("welcome to china");
    person->set_datafix32(1);
    person->set_datafix64(666);
    person->set_datasfix32(120);
    person->set_datasfix64(-802);

    std::string info1;
    std::string error;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(*person, &text);

    printf("----------test pb to json------------\n\n");
    bool ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_TRUE(ret);
    AddressBook address_book_test;
    ret = json2pb::JsonToProtoMessage(info1, &address_book_test, &error);
    ASSERT_TRUE(ret);
    std::string info2;
    ret = json2pb::ProtoMessageToJson(address_book_test, &info2, &error);
    ASSERT_TRUE(ret);
    ASSERT_TRUE(!info1.compare(info2));
    butil::IOBuf buf;
    butil::IOBufAsZeroCopyOutputStream stream(&buf); 
    bool res = json2pb::ProtoMessageToJson(address_book, &stream, NULL);
    ASSERT_TRUE(res);
    butil::IOBufAsZeroCopyInputStream stream2(buf); 
    AddressBook address_book_test3;
    ret = json2pb::JsonToProtoMessage(&stream2, &address_book_test3, &error);
    ASSERT_TRUE(ret);
    std::string info3;
    ret = json2pb::ProtoMessageToJson(address_book_test3, &info3, &error);
    ASSERT_TRUE(ret);
    ASSERT_TRUE(!info2.compare(info3));
}

TEST_F(ProtobufJsonTest, json_to_pb_edge_case) {
    std::string info3 = "{\"judge\":false, \"spur\":2.0e1}"; 
    
    std::string error; 
    JsonContextBody data;
    bool ret = json2pb::JsonToProtoMessage(info3, &data, &error);
    ASSERT_TRUE(ret);

    std::string info4;  
    std::string error1;
    ret = json2pb::ProtoMessageToJson(data, &info4, &error1);
    ASSERT_TRUE(ret);
    
    info3 = "{\"judge\":false, \"spur\":-2, \"data\":[], \"info\":[],\"content\":[]}"; 
    error.clear(); 

    JsonContextBody data1;
    ret = json2pb::JsonToProtoMessage(info3, &data1, &error);
    ASSERT_TRUE(ret);
   
    info4.clear();  
    error1.clear();
    ret = json2pb::ProtoMessageToJson(data1, &info4, &error1);
    ASSERT_TRUE(ret);

    info3 = "{\"judge\":false, \"spur\":\"NaN\"}"; 
    error.clear(); 

    JsonContextBody data2;
    ret = json2pb::JsonToProtoMessage(info3, &data2, &error);
    ASSERT_TRUE(ret);
   
    info4.clear();  
    error1.clear();
    ret = json2pb::ProtoMessageToJson(data2, &info4, &error1);
    ASSERT_TRUE(ret);

    info3 = "{\"judge\":false, \"spur\":\"Infinity\"}"; 
    error.clear(); 

    JsonContextBody data3;
    ret = json2pb::JsonToProtoMessage(info3, &data3, &error);
    ASSERT_TRUE(ret);
   
    info4.clear();  
    error1.clear();
    ret = json2pb::ProtoMessageToJson(data3, &info4, &error1);
    ASSERT_TRUE(ret);

    info3 = "{\"judge\":false, \"spur\":\"-inFiNITY\"}"; 
    error.clear(); 

    JsonContextBody data4;
    ret = json2pb::JsonToProtoMessage(info3, &data4, &error);
    ASSERT_TRUE(ret);
   
    info4.clear();  
    error1.clear();
    ret = json2pb::ProtoMessageToJson(data4, &info4, &error1);
    ASSERT_TRUE(ret);

    info3 = "{\"judge\":false, \"spur\":2.0, \"content\":[{\"distance\":2.5, "
            "\"ext\":{\"databyte\":\"d2VsY29tZQ==\", \"enumtype\":\"MOBILE\"}}]}"; 
    error.clear(); 

    JsonContextBody data5;
    ret = json2pb::JsonToProtoMessage(info3, &data5, &error);
    ASSERT_TRUE(ret);
   
    info4.clear();  
    error1.clear();
    ret = json2pb::ProtoMessageToJson(data5, &info4, &error1);
    ASSERT_TRUE(ret);
    
    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":2.3,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":\"Test\"},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 

    JsonContextBody data9;
    ret = json2pb::JsonToProtoMessage(info3, &data9, &error);
    ASSERT_TRUE(ret); 
    ASSERT_STREQ("Invalid value `\"Test\"' for optional field `Ext.enumtype' which SHOULD be enum", error.data());
    
    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":5,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":15},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 

    JsonContextBody data10;
    ret = json2pb::JsonToProtoMessage(info3, &data10, &error);
    ASSERT_TRUE(ret); 
    ASSERT_STREQ("Invalid value `15' for optional field `Ext.enumtype' which SHOULD be enum", error.data());

    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":5,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":15},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"type\":[\"123\"]}";  
    error.clear(); 

    JsonContextBody data11;
    ret = json2pb::JsonToProtoMessage(info3, &data11, &error);
    ASSERT_TRUE(ret);   
    ASSERT_STREQ("Invalid value `array' for optional field `JsonContextBody.type' which SHOULD be INT64, Invalid value `15' for optional field `Ext.enumtype' which SHOULD be enum", 
                 error.data());
}

TEST_F(ProtobufJsonTest, json_to_pb_expected_failed_case) {
    std::string info3 = "{\"content\":[{\"unknown_member\":2,\"ext\":{\"age\":1666666666, \
                          \"databyte\":\"welcome\", \"enumtype\":1},\"uid\":\"someone\"},\
                          {\"unknown_member\":20,\"ext\":{\"age\":1666666660, \"databyte\":\
                          \"welcome0\", \"enumtype\":2},\"uid\":\"someone0\"}], \
                          \"judge\":false, \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    std::string error; 

    JsonContextBody data;
    bool ret = json2pb::JsonToProtoMessage(info3, &data, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: Content.distance", error.data());
    
    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\"ext\":{\"age\":1666666666, \
              \"enumtype\":1},\"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 
  
    JsonContextBody data2;
    ret = json2pb::JsonToProtoMessage(info3, &data2, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: Ext.databyte", error.data());

    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"welcome\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 

    JsonContextBody data3;
    ret = json2pb::JsonToProtoMessage(info3, &data, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: JsonContextBody.judge", error.data());

    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"welcome\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"judge\":\"false\", \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 
  
    JsonContextBody data4;
    ret = json2pb::JsonToProtoMessage(info3, &data4, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Invalid value `\"false\"' for field `JsonContextBody.judge' which SHOULD be BOOL", error.data());

    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"welcome\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[\"1\",\"2\",\"3\",\"4\"]}";  
    error.clear(); 

    JsonContextBody data5;
    ret = json2pb::JsonToProtoMessage(info3, &data5, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Invalid value `\"1\"' for field `JsonContextBody.data' which SHOULD be INT32", error.data());

    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\
              \"ext\":{\"age\":1666666666, \"databyte\":\"welcome\", \"enumtype\":1},\
              \"uid\":\"someone\"},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10], \"info\":2}";  
    error.clear(); 

    JsonContextBody data6;
    ret = json2pb::JsonToProtoMessage(info3, &data6, &error);
    ASSERT_FALSE(ret); 
    ASSERT_STREQ("Invalid value for repeated field: JsonContextBody.info", error.data());
 
    info3 = "{\"judge\":false, \"spur\":\"NaNa\"}"; 
    error.clear(); 

    JsonContextBody data7;
    ret = json2pb::JsonToProtoMessage(info3, &data7, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Invalid value `\"NaNa\"' for field `JsonContextBody.spur' which SHOULD be d", 
                 error.data());

    info3 = "{\"judge\":false, \"spur\":\"Infinty\"}"; 
    error.clear(); 

    JsonContextBody data8;
    ret = json2pb::JsonToProtoMessage(info3, &data8, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Invalid value `\"Infinty\"' for field `JsonContextBody.spur' which SHOULD be d", 
                 error.data());
    
    info3 = "{\"content\":[{\"distance\":1,\"unknown_member\":2,\"ext\":{\"age\":1666666666, \
              \"enumtype\":1},\"uid\":23},{\"distance\":10,\"unknown_member\":20,\
              \"ext\":{\"age\":1666666660, \"databyte\":\"welcome0\", \"enumtype\":2},\
              \"uid\":\"someone0\"}], \"judge\":false, \
              \"spur\":2, \"data\":[1,2,3,4,5,6,7,8,9,10]}";  
    error.clear(); 
  
    JsonContextBody data9;
    ret = json2pb::JsonToProtoMessage(info3, &data9, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Invalid value `23' for optional field `Content.uid' which SHOULD be string, Missing required field: Ext.databyte", error.data());
}

TEST_F(ProtobufJsonTest, json_to_pb_perf_case) {
    
    std::string info3 = "{\"content\":[{\"distance\":1.0,\
                          \"ext\":{\"age\":1666666666, \"databyte\":\"d2VsY29tZQ==\", \"enumtype\":1},\
                          \"uid\":\"welcome\"}], \"judge\":false, \"spur\":2.0, \"data\":[]}";  

    printf("----------test json to pb performance------------\n\n");

    std::string error; 
  
    ProfilerStart("json_to_pb_perf.prof");
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 100000;
    for (int i = 0; i < times; i++) { 
        JsonContextBody data;
        timer.start();
        res = json2pb::JsonToProtoMessage(info3, &data, &error);
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);

        std::string info4;  
        std::string error1;
        timer.start();
        res = json2pb::ProtoMessageToJson(data, &info4, &error1);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time1 /= times;
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert json to pb is %fus\n", avg_time1);
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, json_to_pb_encode_decode_perf_case) {
    std::string info3 = "{\"@Content_Test%@\":[{\"Distance_info_\":1,\
                          \"_ext%T_\":{\"Aa_ge(\":1666666666, \"databyte(std::string)\":\
                          \"welcome\", \"enum--type\":1},\"uid*\":\"welcome\"}], \
                          \"judge\":false, \"spur\":2, \"data:array\":[]}";  
    printf("----------test json to pb encode/decode performance------------\n\n");
    std::string error; 
    
    ProfilerStart("json_to_pb_encode_decode_perf.prof");
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 100000;
    for (int i = 0; i < times; i++) { 
        JsonContextBody data; 
        timer.start();
        res = json2pb::JsonToProtoMessage(info3, &data, &error);
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);

        std::string info4;  
        std::string error1;
        timer.start();
        res = json2pb::ProtoMessageToJson(data, &info4, &error1);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time1 /= times;
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert json to pb is %fus\n", avg_time1);
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, json_to_pb_complex_perf_case) {
    std::ifstream in("jsonout", std::ios::in);
    std::ostringstream tmp;
    tmp << in.rdbuf();
    butil::IOBuf buf;
    buf.append(tmp.str());
    in.close();

    printf("----------test json to pb performance------------\n\n");

    std::string error; 
  
    butil::Timer timer;

    bool res;
    float avg_time1 = 0;
    const int times = 10000;
    json2pb::Json2PbOptions options;
    options.base64_to_bytes = false;
    ProfilerStart("json_to_pb_complex_perf.prof");
    for (int i = 0; i < times; i++) { 
        gss::message::gss_us_res_t data;
        butil::IOBufAsZeroCopyInputStream stream(buf); 
        timer.start();
        res = json2pb::JsonToProtoMessage(&stream, &data, options, &error);
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    ProfilerStop();
    avg_time1 /= times;
    printf("avg time to convert json to pb is %fus\n", avg_time1);
}

TEST_F(ProtobufJsonTest, json_to_pb_to_string_complex_perf_case) {
    std::ifstream in("jsonout", std::ios::in);
    std::ostringstream tmp;
    tmp << in.rdbuf();
    std::string info3 = tmp.str();
    in.close();

    printf("----------test json to pb performance------------\n\n");

    std::string error; 
  
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    const int times = 10000;
    json2pb::Json2PbOptions options;
    options.base64_to_bytes = false;
    ProfilerStart("json_to_pb_to_string_complex_perf.prof");
    for (int i = 0; i < times; i++) { 
        gss::message::gss_us_res_t data;
        timer.start();
        res = json2pb::JsonToProtoMessage(info3, &data, options, &error);
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time1 /= times;
    ProfilerStop();
    printf("avg time to convert json to pb is %fus\n", avg_time1);
}

TEST_F(ProtobufJsonTest, pb_to_json_normal_case) {
    AddressBook address_book;

    Person* person = address_book.add_person();
 
    person->set_id(100);
    person->set_name("baidu");
    person->set_email("welcome@baidu.com");  

    Person::PhoneNumber* phone_number = person->add_phone();
    phone_number->set_number("number123");
    phone_number->set_type(Person::HOME);
 
    person->set_data(-240000000);
    person->set_data32(6);
    person->set_data64(-1820000000);
    person->set_datadouble(123.456);
    person->set_datadouble_scientific(1.23456789e+08);
    person->set_datafloat_scientific(1.23456789e+08);
    person->set_datafloat(8.6123);
    person->set_datau32(60);
    person->set_datau64(960);
    person->set_databool(0);
    person->set_databyte("welcome");
    person->set_datafix32(1);
    person->set_datafix64(666);
    person->set_datasfix32(120);
    person->set_datasfix64(-802);

    std::string info1;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(*person, &text);

    printf("text:%s\n", text.data());

    printf("----------test pb to json------------\n\n");
    json2pb::Pb2JsonOptions option;
    option.bytes_to_base64 = true;
    bool ret = json2pb::ProtoMessageToJson(address_book, &info1, option, NULL);
    ASSERT_TRUE(ret);

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info1.data());
#endif

    info1.clear();
    {
        json2pb::Pb2JsonOptions option;
        option.bytes_to_base64 = true;
        ret = ProtoMessageToJson(address_book, &info1, option, NULL);
    }
    ASSERT_TRUE(ret);

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info1.data());
#endif
    
    info1.clear();
    {
        json2pb::Pb2JsonOptions option;
        option.bytes_to_base64 = true;
        option.enum_option = json2pb::OUTPUT_ENUM_BY_NUMBER;
        ret = ProtoMessageToJson(address_book, &info1, option, NULL);
    }
    ASSERT_TRUE(ret);

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":1}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":1}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info1.data());
#endif

    printf("----------test json to pb------------\n\n");

    const int N = 1000;
    int64_t total_tm = 0;
    int64_t total_tm2 = 0;
    for (int i = 0; i < N; ++i) {

        std::string info3;
        AddressBook data1;
        std::string error1;
        const int64_t tm1 = gettimeofday_us();
        bool ret1 = json2pb::JsonToProtoMessage(info1, &data1, &error1); 
        const int64_t tm2 = gettimeofday_us();
        total_tm += tm2 - tm1;
        ASSERT_TRUE(ret1);
        
        std::string error2;
        const int64_t tm3 = gettimeofday_us();
        ret1 = json2pb::ProtoMessageToJson(data1, &info3, &error2);
        const int64_t tm4 = gettimeofday_us();
        ASSERT_TRUE(ret1);
        total_tm2 += tm4 - tm3;
#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info3.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu\",\"id\":100,\"email\":\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"d2VsY29tZQ==\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info3.data());
#endif
}
    std::cout << "json2pb=" << total_tm / N
              << "us pb2json=" << total_tm2 / N << "us"
              << std::endl;
}

TEST_F(ProtobufJsonTest, pb_to_json_map_case) {
    std::string json = "{\"addr\":\"baidu.com\","
                       "\"numbers\":{\"tel\":123456,\"cell\":654321},"
                       "\"contacts\":{\"email\":\"frank@baidu.com\","
                       "               \"office\":\"Shanghai\"},"
                       "\"friends\":{\"John\":[{\"school\":\"SJTU\",\"year\":2007}]}}";
    std::string output;
    std::string error;
    AddressNoMap ab1;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab1, &error));
    ASSERT_TRUE(json2pb::ProtoMessageToJson(ab1, &output, &error));
    ASSERT_TRUE(output.find("\"addr\":\"baidu.com\"") != std::string::npos);

    output.clear();
    AddressIntMap ab2;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab2, &error));
    ASSERT_TRUE(json2pb::ProtoMessageToJson(ab2, &output, &error));
    ASSERT_TRUE(output.find("\"addr\":\"baidu.com\"") != std::string::npos);
    ASSERT_TRUE(output.find("\"tel\":123456") != std::string::npos);
    ASSERT_TRUE(output.find("\"cell\":654321") != std::string::npos);
    
    output.clear();
    AddressStringMap ab3;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab3, &error));
    ASSERT_TRUE(json2pb::ProtoMessageToJson(ab3, &output, &error));
    ASSERT_TRUE(output.find("\"addr\":\"baidu.com\"") != std::string::npos);
    ASSERT_TRUE(output.find("\"email\":\"frank@baidu.com\"") != std::string::npos);
    ASSERT_TRUE(output.find("\"office\":\"Shanghai\"") != std::string::npos);

    output.clear();
    AddressComplex ab4;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &ab4, &error));
    ASSERT_TRUE(json2pb::ProtoMessageToJson(ab4, &output, &error));
    ASSERT_TRUE(output.find("\"addr\":\"baidu.com\"") != std::string::npos);
    ASSERT_TRUE(output.find("\"friends\":{\"John\":[{\"school\":\"SJTU\","
                            "\"year\":2007}]}") != std::string::npos);
}

TEST_F(ProtobufJsonTest, pb_to_json_encode_decode) {
    JsonContextBodyEncDec json_data;
    json_data.set_type(80000);
    json_data.add_data_z058_array(200);
    json_data.add_data_z058_array(300);
    json_data.add_info("this is json data's info");
    json_data.add_info("this is a test");
    json_data.set_judge(true);
    json_data.set_spur(3.45);
    
    ContentEncDec * content = json_data.add__z064_content_test_z037__z064_();
    content->set_uid_z042_("content info");
    content->set_distance_info_(1234.56);
    
    ExtEncDec* ext = content->mutable__ext_z037_t_(); 
    ext->set_aa_ge_z040_(160000);
    ext->set_databyte_z040_std_z058__z058_string_z041_("databyte"); 
    ext->set_enum_z045__z045_type(ExtEncDec_PhoneTypeEncDec_WORK);

    std::string info1;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(json_data, &text);

    printf("text:%s\n", text.data());

    printf("----------test pb to json------------\n\n");
    json2pb::Pb2JsonOptions option;
    option.bytes_to_base64 = true;
    ASSERT_TRUE(ProtoMessageToJson(json_data, &info1, option, NULL));
#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"info\":[\"this is json data's info\",\"this is a test\"],\"type\":80000,"
                 "\"data:array\":[200,300],\"judge\":true,\"spur\":3.45,\"@Content_Test%@\":"
                 "[{\"uid*\":\"content info\",\"Distance_info_\":1234.56005859375,\"_ext%T_\":"
                 "{\"Aa_ge(\":160000,\"databyte(std::string)\":\"ZGF0YWJ5dGU=\","
                 "\"enum--type\":\"WORK\"}}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"info\":[\"this is json data's info\",\"this is a test\"],\"type\":80000,"
                 "\"data:array\":[200,300],\"judge\":true,\"spur\":3.45,\"@Content_Test%@\":"
                 "[{\"uid*\":\"content info\",\"Distance_info_\":1234.560059,\"_ext%T_\":"
                 "{\"Aa_ge(\":160000,\"databyte(std::string)\":\"ZGF0YWJ5dGU=\","
                 "\"enum--type\":\"WORK\"}}]}", 
                 info1.data());
#endif
    printf("----------test json to pb------------\n\n");
    
    std::string info3;
    JsonContextBody data1;
    json2pb::JsonToProtoMessage(info1, &data1, NULL); 
    json2pb::ProtoMessageToJson(data1, &info3, NULL);
#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"info\":[\"this is json data's info\",\"this is a test\"],\"type\":80000,"
                 "\"data:array\":[200,300],\"judge\":true,\"spur\":3.45,\"@Content_Test%@\":"
                 "[{\"uid*\":\"content info\",\"Distance_info_\":1234.56005859375,\"_ext%T_\":"
                 "{\"Aa_ge(\":160000,\"databyte(std::string)\":\"ZGF0YWJ5dGU=\","
                 "\"enum--type\":\"WORK\"}}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"info\":[\"this is json data's info\",\"this is a test\"],\"type\":80000,"
                 "\"data:array\":[200,300],\"judge\":true,\"spur\":3.45,\"@Content_Test%@\":"
                 "[{\"uid*\":\"content info\",\"Distance_info_\":1234.560059,\"_ext%T_\":"
                 "{\"Aa_ge(\":160000,\"databyte(std::string)\":\"ZGF0YWJ5dGU=\","
                 "\"enum--type\":\"WORK\"}}]}", 
                 info1.data());
#endif
}

TEST_F(ProtobufJsonTest, pb_to_json_control_char_case) {
    AddressBook address_book;

    Person* person = address_book.add_person();
 
    person->set_id(100);
    char ch = 0x01;
    char* name = new char[17];
    memcpy(name, "baidu ", 6);
    name[6] = ch;
    char c = 0x08;
    char t = 0x1A;
    memcpy(name + 7, "test", 4);
    name[11] = c;
    name[12] = t;
    memcpy(name + 13, "end", 3);
    name[16] = '\0';
    person->set_name(name);
    printf("name is %s\n", name);
    person->set_email("welcome@baidu.com");  

    Person::PhoneNumber* phone_number = person->add_phone();
    phone_number->set_number("number123");
    phone_number->set_type(Person::HOME);
 
    person->set_data(-240000000);
    person->set_data32(6);
    person->set_data64(-1820000000);
    person->set_datadouble(123.456);
    person->set_datadouble_scientific(1.23456789e+08);
    person->set_datafloat_scientific(1.23456789e+08);
    person->set_datafloat(8.6123);
    person->set_datau32(60);
    person->set_datau64(960);
    person->set_databool(0);
    person->set_databyte("welcome to china");
    person->set_datafix32(1);
    person->set_datafix64(666);
    person->set_datasfix32(120);
    person->set_datasfix64(-802);

    std::string info1;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(*person, &text);

    printf("text:%s\n", text.data());

    bool ret = false;
    printf("----------test pb to json------------\n\n");
    {
        json2pb::Pb2JsonOptions option;
        option.bytes_to_base64 = false;
        ret = ProtoMessageToJson(address_book, &info1, option, NULL);
        ASSERT_TRUE(ret);
    }

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"welcome to china\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"welcome to china\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info1.data());
#endif

    info1.clear();
    {
        json2pb::Pb2JsonOptions option;
        option.bytes_to_base64 = true;
        ret = ProtoMessageToJson(address_book, &info1, option, NULL);
        ASSERT_TRUE(ret);
    }

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"d2VsY29tZSB0byBjaGluYQ==\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":\"HOME\"}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,\"databool\":false,"
                 "\"databyte\":\"d2VsY29tZSB0byBjaGluYQ==\",\"datafix32\":1,\"datafix64\":666,"
                 "\"datasfix32\":120,\"datasfix64\":-802,\"datafloat_scientific\":123456792,"
                 "\"datadouble_scientific\":123456789}]}", info1.data());
#endif
    
    info1.clear();
    {
        json2pb::Pb2JsonOptions option;
        option.enum_option = json2pb::OUTPUT_ENUM_BY_NUMBER;
        option.bytes_to_base64 = false;
        ret = ProtoMessageToJson(address_book, &info1, option, NULL);
        ASSERT_TRUE(ret);
    }

#ifndef RAPIDJSON_VERSION_0_1
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":1}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919128418,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"welcome to china\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792.0,\"datadouble_scientific\":123456789.0}]}", 
                 info1.data());
#else
    std::cout << info1.data() << std::endl;
    ASSERT_STREQ("{\"person\":[{\"name\":\"baidu \\u0001test\\b\\u001Aend\",\"id\":100,\"email\":"
                 "\"welcome@baidu.com\","
                 "\"phone\":[{\"number\":\"number123\",\"type\":1}],\"data\":-240000000,"
                 "\"data32\":6,\"data64\":-1820000000,\"datadouble\":123.456,"
                 "\"datafloat\":8.612299919,\"datau32\":60,\"datau64\":960,"
                 "\"databool\":false,\"databyte\":\"welcome to china\",\"datafix32\":1,"
                 "\"datafix64\":666,\"datasfix32\":120,\"datasfix64\":-802,"
                 "\"datafloat_scientific\":123456792,\"datadouble_scientific\":123456789}]}", 
                 info1.data());
#endif
}

TEST_F(ProtobufJsonTest, pb_to_json_unicode_case) {
    AddressBook address_book;

    Person* person = address_book.add_person();
 
    person->set_id(100);
    
    char name[255*1024];
    for (int j = 0; j < 1024; j++) {
        for (int i = 0; i < 255; i++) {
            name[j*255 + i] = i + 1;
        }
    }
    name[255*1024 - 1] = '\0';
    person->set_name(name);
    person->set_data(-240000000);
    person->set_data32(6);
    person->set_data64(-1820000000);
    person->set_datadouble(123.456);
    person->set_datadouble_scientific(1.23456789e+08);
    person->set_datafloat_scientific(1.23456789e+08);
    person->set_datafloat(8.6123);
    person->set_datau32(60);
    person->set_datau64(960);
    person->set_databool(0);
    person->set_databyte("welcome to china");
    person->set_datafix32(1);
    person->set_datafix64(666);
    person->set_datasfix32(120);
    person->set_datasfix64(-802);

    std::string info1;
    std::string error;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(*person, &text);

    printf("----------test pb to json------------\n\n");
    bool ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_TRUE(ret);
    butil::IOBuf buf;
    butil::IOBufAsZeroCopyOutputStream stream(&buf); 
    bool res = json2pb::ProtoMessageToJson(address_book, &stream, NULL);
    ASSERT_TRUE(res);
    ASSERT_TRUE(!info1.compare(buf.to_string()));
}
                 
TEST_F(ProtobufJsonTest, pb_to_json_edge_case) {
    AddressBook address_book;
    
    std::string info1;
    std::string error;
    bool ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_TRUE(ret);
   
    info1.clear();
    Person* person = address_book.add_person();
      
    person->set_id(100);
    person->set_name("baidu");

    Person::PhoneNumber* phone_number = person->add_phone();
    phone_number->set_number("1234556");

    person->set_datadouble(-345.67);
    person->set_datafloat(8.6123);

    ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);

    ASSERT_TRUE(ret);
    ASSERT_TRUE(error.empty());

    std::string info3;
    AddressBook data1;
    std::string error1;
    bool ret1 = json2pb::JsonToProtoMessage(info1, &data1, &error1); 
    ASSERT_TRUE(ret1);
    ASSERT_TRUE(error1.empty());

    std::string error2;
    bool ret2 = json2pb::ProtoMessageToJson(data1, &info3, &error2);
    ASSERT_TRUE(ret2);
    ASSERT_TRUE(error2.empty());
}

TEST_F(ProtobufJsonTest, pb_to_json_expected_failed_case) {
    AddressBook address_book;
    std::string info1; 
    std::string error;

    Person* person = address_book.add_person();
 
    person->set_name("baidu");
    person->set_email("welcome@baidu.com");  
    
    bool ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: addressbook.Person.id", error.data()); 
     
    address_book.clear_person();
    person = address_book.add_person();

    person->set_id(2);
    person->set_email("welcome@baidu.com");  
    
    ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: addressbook.Person.name", error.data()); 

    address_book.clear_person();
    person = address_book.add_person();

    person->set_id(2);
    person->set_name("name");
    person->set_email("welcome@baidu.com");  
    
    ret = json2pb::ProtoMessageToJson(address_book, &info1, &error);
    ASSERT_FALSE(ret);
    ASSERT_STREQ("Missing required field: addressbook.Person.datadouble", error.data()); 
}

TEST_F(ProtobufJsonTest, pb_to_json_perf_case) {
    AddressBook address_book;

    Person* person = address_book.add_person();
 
    person->set_id(100);
    person->set_name("baidu");
    person->set_email("welcome@baidu.com");  

    Person::PhoneNumber* phone_number = person->add_phone();
    phone_number->set_number("number123");
    phone_number->set_type(Person::HOME);
 
    person->set_data(-240000000);

    person->set_data32(6);

    person->set_data64(-1820000000);

    person->set_datadouble(123.456);

    person->set_datadouble_scientific(1.23456789e+08);

    person->set_datafloat_scientific(1.23456789e+08);

    person->set_datafloat(8.6123);

    person->set_datau32(60);

    person->set_datau64(960);

    person->set_databool(0);

    person->set_databyte("welcome to china");

    person->set_datafix32(1);

    person->set_datafix64(666);

    person->set_datasfix32(120);

    person->set_datasfix64(-802);

    std::string info1;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(*person, &text);

    printf("text:%s\n", text.data());

    printf("----------test pb to json performance------------\n\n");
    ProfilerStart("pb_to_json_perf.prof");
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 100000;
    ASSERT_TRUE(json2pb::ProtoMessageToJson(address_book, &info1, NULL));
    for (int i = 0; i < times; i++) { 
        std::string info3;
        AddressBook data1;
        timer.start();
        res = json2pb::JsonToProtoMessage(info1, &data1, NULL); 
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);
        
        timer.start();
        res = json2pb::ProtoMessageToJson(data1, &info3, NULL);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time1 /= times;
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert json to pb is %fus\n", avg_time1);
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, pb_to_json_encode_decode_perf_case) {
    JsonContextBodyEncDec json_data;
    json_data.set_type(80000);
    json_data.add_data_z058_array(200);
    json_data.add_data_z058_array(300);
    json_data.add_info("this is json data's info");
    json_data.add_info("this is a test");
    json_data.set_judge(true);
    json_data.set_spur(3.45);
    
    ContentEncDec * content = json_data.add__z064_content_test_z037__z064_();
    content->set_uid_z042_("content info");
    content->set_distance_info_(1234.56);
    
    ExtEncDec* ext = content->mutable__ext_z037_t_(); 
    ext->set_aa_ge_z040_(160000);
    ext->set_databyte_z040_std_z058__z058_string_z041_("databyte"); 
    ext->set_enum_z045__z045_type(ExtEncDec_PhoneTypeEncDec_WORK);

    std::string info1;

    google::protobuf::TextFormat::Printer printer;
    std::string text;
    printer.PrintToString(json_data, &text);

    printf("text:%s\n", text.data());
    
    ASSERT_TRUE(json2pb::ProtoMessageToJson(json_data, &info1, NULL));

    printf("----------test pb to json encode decode performance------------\n\n");
    ProfilerStart("pb_to_json_encode_decode_perf.prof");
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 100000;
    for (int i = 0; i < times; i++) { 
        std::string info3;
        JsonContextBody json_body;
        timer.start();
        res = json2pb::JsonToProtoMessage(info1, &json_body, NULL); 
        timer.stop();
        avg_time1 += timer.u_elapsed();
        ASSERT_TRUE(res);
        
        timer.start();
        res = json2pb::ProtoMessageToJson(json_body, &info3, NULL);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time1 /= times;
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert json to pb is %fus\n", avg_time1);
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, pb_to_json_complex_perf_case) {

    std::ifstream in("jsonout", std::ios::in);
    std::ostringstream tmp;
    tmp << in.rdbuf();
    std::string info3 = tmp.str();
    in.close();

    printf("----------test pb to json performance------------\n\n");

    std::string error;

    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 10000;
    gss::message::gss_us_res_t data;
    json2pb::Json2PbOptions option;
    option.base64_to_bytes = false;
    timer.start();
    res = JsonToProtoMessage(info3, &data, option, &error);
    timer.stop();
    avg_time1 += timer.u_elapsed();
    ASSERT_TRUE(res) << error;
    ProfilerStart("pb_to_json_complex_perf.prof");
    for (int i = 0; i < times; i++) { 
        std::string error1;
        timer.start();
        butil::IOBuf buf;
        butil::IOBufAsZeroCopyOutputStream stream(&buf); 
        res = json2pb::ProtoMessageToJson(data, &stream, &error1);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, pb_to_json_to_string_complex_perf_case) {
    std::ifstream in("jsonout", std::ios::in);
    std::ostringstream tmp;
    tmp << in.rdbuf();
    std::string info3 = tmp.str();
    in.close();

    printf("----------test pb to json performance------------\n\n");

    std::string error; 
  
    butil::Timer timer;
    bool res;
    float avg_time1 = 0;
    float avg_time2 = 0;
    const int times = 10000;
    gss::message::gss_us_res_t data;
    json2pb::Json2PbOptions option;
    option.base64_to_bytes = false;
    timer.start();
    res = JsonToProtoMessage(info3, &data, option, &error);
    timer.stop();
    avg_time1 += timer.u_elapsed();
    ASSERT_TRUE(res);
    ProfilerStart("pb_to_json_to_string_complex_perf.prof");
    for (int i = 0; i < times; i++) { 
        std::string info4;  
        std::string error1;
        timer.start();
        std::string inf4;
        res = json2pb::ProtoMessageToJson(data, &info4, &error1);
        timer.stop();
        avg_time2 += timer.u_elapsed();
        ASSERT_TRUE(res);
    }
    avg_time2 /= times;
    ProfilerStop();
    printf("avg time to convert pb to json is %fus\n", avg_time2);
}

TEST_F(ProtobufJsonTest, encode_decode_case) {
      
    std::string json_key = "abcdek123lske_slkejfl_l1kdle";
    std::string field_name;
    ASSERT_FALSE(json2pb::encode_name(json_key, field_name));
    ASSERT_TRUE(field_name.empty());
    std::string json_key_decode; 
    ASSERT_FALSE(json2pb::decode_name(field_name, json_key_decode));
    ASSERT_TRUE(json_key_decode.empty());
    
    json_key = "_Afledk2e*_+%leGi___hE_Z278_t#";
    field_name.clear();
    json2pb::encode_name(json_key, field_name);
    const char* encode_json_key = "_Afledk2e_Z042___Z043__Z037_leGi___hE_Z278_t_Z035_";
    ASSERT_TRUE(strcmp(field_name.data(), encode_json_key) == 0);
    json_key_decode.clear();
    json2pb::decode_name(field_name, json_key_decode);
    ASSERT_TRUE(strcmp(json_key_decode.data(), json_key.data()) == 0);
    
    json_key = "_ext%T_";
    field_name.clear();
    json2pb::encode_name(json_key, field_name);
    encode_json_key = "_ext_Z037_T_";
    ASSERT_TRUE(strcmp(field_name.data(), encode_json_key) == 0);
    json_key_decode.clear();
    json2pb::decode_name(field_name, json_key_decode);
    ASSERT_TRUE(strcmp(json_key_decode.data(), json_key.data()) == 0);

    std::string empty_key;
    std::string empty_result;
    ASSERT_FALSE(json2pb::encode_name(empty_key, empty_result));
    ASSERT_TRUE(empty_result.empty() == true);
    ASSERT_FALSE(json2pb::decode_name(empty_result, empty_key));
    ASSERT_TRUE(empty_key.empty() == true);
}
                 
TEST_F(ProtobufJsonTest, json_to_zero_copy_stream_normal_case) {
    Person person;
    person.set_name("hello");
    person.set_id(9);
    person.set_datadouble(2.2);
    person.set_datafloat(1);
    butil::IOBuf iobuf;
    butil::IOBufAsZeroCopyOutputStream wrapper(&iobuf);
    std::string error;
    ASSERT_TRUE(json2pb::ProtoMessageToJson(person, &wrapper, &error)) << error;
    std::string out = iobuf.to_string();
    ASSERT_EQ("{\"name\":\"hello\",\"id\":9,\"datadouble\":2.2,\"datafloat\":1.0}", out);
}

TEST_F(ProtobufJsonTest, zero_copy_stream_to_json_normal_case) {
    butil::IOBuf iobuf;
    iobuf = "{\"name\":\"hello\",\"id\":9,\"datadouble\":2.2,\"datafloat\":1.0}";
    butil::IOBufAsZeroCopyInputStream wrapper(iobuf);
    Person person;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(&wrapper, &person));
    ASSERT_STREQ("hello", person.name().c_str());
    ASSERT_EQ(9, person.id());
    ASSERT_EQ(2.2, person.datadouble());
    ASSERT_EQ(1, person.datafloat());
}

TEST_F(ProtobufJsonTest, extension_case) {
    std::string json = "{\"name\":\"hello\",\"id\":9,\"datadouble\":2.2,\"datafloat\":1.0,\"hobby\":\"coding\"}";
    Person person;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &person));
    ASSERT_STREQ("coding", person.GetExtension(addressbook::hobby).data());
    std::string output;
    ASSERT_TRUE(json2pb::ProtoMessageToJson(person, &output));
    ASSERT_EQ("{\"hobby\":\"coding\",\"name\":\"hello\",\"id\":9,\"datadouble\":2.2,\"datafloat\":1.0}", output);
}

TEST_F(ProtobufJsonTest, string_to_int64) {
    auto json = R"({"name":"hello", "id":9, "data": "123456", "datadouble":2.2, "datafloat":1.0})";
    Person person;
    std::string err;
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &person, &err)) << err;
    ASSERT_EQ(person.data(), 123456);
    json = R"({"name":"hello", "id":9, "data": 1234567, "datadouble":2.2, "datafloat":1.0})";
    ASSERT_TRUE(json2pb::JsonToProtoMessage(json, &person));
    ASSERT_EQ(person.data(), 1234567);
}

}
