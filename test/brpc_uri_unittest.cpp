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

#include "brpc/uri.h"

TEST(URITest, everything) {
    brpc::URI uri;
    std::string uri_str = " foobar://user:passwd@www.baidu.com:80/s?wd=uri#frag  ";
    ASSERT_EQ(0, uri.SetHttpURL(uri_str));
    ASSERT_EQ("foobar", uri.scheme());
    ASSERT_EQ(80, uri.port());
    ASSERT_EQ("www.baidu.com", uri.host());
    ASSERT_EQ("/s", uri.path());
    ASSERT_EQ("user:passwd", uri.user_info());
    ASSERT_EQ("frag", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri");
    ASSERT_FALSE(uri.GetQuery("nonkey"));

    std::string scheme;
    std::string host_out;
    int port_out = -1;
    brpc::ParseURL(uri_str.c_str(), &scheme, &host_out, &port_out);
    ASSERT_EQ("foobar", scheme);
    ASSERT_EQ("www.baidu.com", host_out);
    ASSERT_EQ(80, port_out);
}

TEST(URITest, only_host) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL("  foo1://www.baidu1.com?wd=uri2&nonkey=22 "));
    ASSERT_EQ("foo1", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu1.com", uri.host());
    ASSERT_EQ("", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("", uri.fragment());
    ASSERT_EQ(2u, uri.QueryCount());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri2");
    ASSERT_TRUE(uri.GetQuery("nonkey"));
    ASSERT_EQ(*uri.GetQuery("nonkey"), "22");

    ASSERT_EQ(0, uri.SetHttpURL("foo2://www.baidu2.com:1234?wd=uri2&nonkey=22 "));
    ASSERT_EQ("foo2", uri.scheme());
    ASSERT_EQ(1234, uri.port());
    ASSERT_EQ("www.baidu2.com", uri.host());
    ASSERT_EQ("", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("", uri.fragment());
    ASSERT_EQ(2u, uri.QueryCount());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri2");
    ASSERT_TRUE(uri.GetQuery("nonkey"));
    ASSERT_EQ(*uri.GetQuery("nonkey"), "22");

    ASSERT_EQ(0, uri.SetHttpURL(" www.baidu3.com:4321 "));
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(4321, uri.port());
    ASSERT_EQ("www.baidu3.com", uri.host());
    ASSERT_EQ("", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("", uri.fragment());
    ASSERT_EQ(0u, uri.QueryCount());
    
    ASSERT_EQ(0, uri.SetHttpURL(" www.baidu4.com "));
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu4.com", uri.host());
    ASSERT_EQ("", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("", uri.fragment());
    ASSERT_EQ(0u, uri.QueryCount());
}

TEST(URITest, no_scheme) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL(" user:passwd2@www.baidu1.com/s?wd=uri2&nonkey=22#frag "));
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu1.com", uri.host());
    ASSERT_EQ("/s", uri.path());
    ASSERT_EQ("user:passwd2", uri.user_info());
    ASSERT_EQ("frag", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri2");
    ASSERT_TRUE(uri.GetQuery("nonkey"));
    ASSERT_EQ(*uri.GetQuery("nonkey"), "22");
}

TEST(URITest, no_scheme_and_user_info) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL(" www.baidu2.com/s?wd=uri2&nonkey=22#frag "));
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu2.com", uri.host());
    ASSERT_EQ("/s", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("frag", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri2");
    ASSERT_TRUE(uri.GetQuery("nonkey"));
    ASSERT_EQ(*uri.GetQuery("nonkey"), "22");
}

TEST(URITest, no_host) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL(" /sb?wd=uri3#frag2 ")) << uri.status();
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("", uri.host());
    ASSERT_EQ("/sb", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("frag2", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri3");
    ASSERT_FALSE(uri.GetQuery("nonkey"));

    // set_path should do as its name says.
    uri.set_path("/x/y/z/");
    ASSERT_EQ("", uri.scheme());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("", uri.host());
    ASSERT_EQ("/x/y/z/", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("frag2", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri3");
    ASSERT_FALSE(uri.GetQuery("nonkey"));
}

TEST(URITest, consecutive_ampersand) {
    brpc::URI uri;
    uri._query = "&key1=value1&&key3=value3";
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_FALSE(uri.GetQuery("key2"));
    ASSERT_EQ("value1", *uri.GetQuery("key1"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
}

TEST(URITest, only_equality) {
    brpc::URI uri;
    uri._query = "key1=&&key2&&=&key3=value3";
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_EQ("", *uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_EQ("", *uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
}

TEST(URITest, set_query) {
    brpc::URI uri;
    uri._query = "key1=&&key2&&=&key3=value3";
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    // overwrite value
    uri.SetQuery("key3", "value4");
    ASSERT_EQ("value4", *uri.GetQuery("key3"));

    uri.SetQuery("key2", "value2");
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_EQ("value2", *uri.GetQuery("key2"));
}

TEST(URITest, set_h2_path) {
    brpc::URI uri;
    uri.SetH2Path("/dir?key1=&&key2&&=&key3=value3");
    ASSERT_EQ("/dir", uri.path());
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));

    uri.SetH2Path("dir?key1=&&key2&&=&key3=value3");
    ASSERT_EQ("dir", uri.path());
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));

    uri.SetH2Path("/dir?key1=&&key2&&=&key3=value3#frag1");
    ASSERT_EQ("/dir", uri.path());
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
    ASSERT_EQ("frag1", uri.fragment());
}

TEST(URITest, generate_h2_path) {
    brpc::URI uri;
    const std::string ref1 = "/dir?key1=&&key2&&=&key3=value3";
    uri.SetH2Path(ref1);
    ASSERT_EQ("/dir", uri.path());
    ASSERT_EQ(3u, uri.QueryCount());
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
    std::string path1;
    uri.GenerateH2Path(&path1);
    ASSERT_EQ(ref1, path1);

    uri.SetQuery("key3", "value3.3");
    ASSERT_EQ(3u, uri.QueryCount());
    ASSERT_EQ(1u, uri.RemoveQuery("key1"));
    ASSERT_EQ(2u, uri.QueryCount());
    ASSERT_EQ("key2&key3=value3.3", uri.query());
    uri.GenerateH2Path(&path1);
    ASSERT_EQ("/dir?key2&key3=value3.3", path1);    

    const std::string ref2 = "/dir2?key1=&&key2&&=&key3=value3#frag2";
    uri.SetH2Path(ref2);
    ASSERT_EQ("/dir2", uri.path());
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
    ASSERT_EQ("frag2", uri.fragment());
    std::string path2;
    uri.GenerateH2Path(&path2);
    ASSERT_EQ(ref2, path2);

    const std::string ref3 = "/dir3#frag3";
    uri.SetH2Path(ref3);
    ASSERT_EQ("/dir3", uri.path());
    ASSERT_EQ("frag3", uri.fragment());
    std::string path3;
    uri.GenerateH2Path(&path3);
    ASSERT_EQ(ref3, path3);

    const std::string ref4 = "/dir4";
    uri.SetH2Path(ref4);
    ASSERT_EQ("/dir4", uri.path());
    std::string path4;
    uri.GenerateH2Path(&path4);
    ASSERT_EQ(ref4, path4);
}

TEST(URITest, only_one_key) {
    brpc::URI uri;
    uri._query = "key1";
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_EQ("", *uri.GetQuery("key1"));
}

TEST(URITest, empty_host) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL("http://"));
    ASSERT_EQ("", uri.host());
    ASSERT_EQ("", uri.path());
}

TEST(URITest, invalid_spaces) {
    brpc::URI uri;
    ASSERT_EQ(-1, uri.SetHttpURL("foo bar://user:passwd@www.baidu.com:80/s?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in url", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://us er:passwd@www.baidu.com:80/s?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in url", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:pass wd@www.baidu.com:80/s?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in url", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www. baidu.com:80/s?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in url", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/ s?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in path", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s ?wd=uri#frag"));
    ASSERT_STREQ("Invalid space in path", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s? wd=uri#frag"));
    ASSERT_STREQ("Invalid space in query", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s?w d=uri#frag"));
    ASSERT_STREQ("Invalid space in query", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s?wd=uri #frag"));
    ASSERT_STREQ("Invalid space in query", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s?wd=uri# frag"));
    ASSERT_STREQ("Invalid space in fragment", uri.status().error_cstr());
    ASSERT_EQ(-1, uri.SetHttpURL("foobar://user:passwd@www.baidu.com:80/s?wd=uri#fr ag"));
    ASSERT_STREQ("Invalid space in fragment", uri.status().error_cstr());
}

TEST(URITest, invalid_query) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL("http://a.b.c/?a-b-c:def"));
    ASSERT_EQ("a-b-c:def", uri.query());
}

TEST(URITest, print_url) {
    brpc::URI uri;

    const std::string url1 = "http://user:passwd@a.b.c/?d=c&a=b&e=f#frg1";
    ASSERT_EQ(0, uri.SetHttpURL(url1));
    std::ostringstream oss;
    uri.Print(oss);
    ASSERT_EQ("http://a.b.c/?d=c&a=b&e=f#frg1", oss.str());
    oss.str("");
    uri.PrintWithoutHost(oss);
    ASSERT_EQ("/?d=c&a=b&e=f#frg1", oss.str());

    const std::string url2 = "http://a.b.c/?d=c&a=b&e=f#frg1";
    ASSERT_EQ(0, uri.SetHttpURL(url2));
    oss.str("");
    uri.Print(oss);
    ASSERT_EQ(url2, oss.str());
    oss.str("");
    uri.PrintWithoutHost(oss);
    ASSERT_EQ("/?d=c&a=b&e=f#frg1", oss.str());

    uri.SetQuery("e", "f2");
    uri.SetQuery("f", "g");
    ASSERT_EQ((size_t)1, uri.RemoveQuery("a"));
    oss.str("");
    uri.Print(oss);
    ASSERT_EQ("http://a.b.c/?d=c&e=f2&f=g#frg1", oss.str());
    oss.str("");
    uri.PrintWithoutHost(oss);
    ASSERT_EQ("/?d=c&e=f2&f=g#frg1", oss.str());
}

TEST(URITest, copy_and_assign) {
    brpc::URI uri;
    const std::string url = "http://user:passwd@a.b.c/?d=c&a=b&e=f#frg1";
    ASSERT_EQ(0, uri.SetHttpURL(url));
    brpc::URI uri2 = uri;
}

TEST(URITest, query_remover_sanity) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ASSERT_EQ(qr.key(), "key1");
    ASSERT_EQ(qr.value(), "value1");
    ++qr;
    ASSERT_EQ(qr.key(), "key2");
    ASSERT_EQ(qr.value(), "value2");
    ++qr;
    ASSERT_EQ(qr.key(), "key3");
    ASSERT_EQ(qr.value(), "value3");
    ++qr;
    ASSERT_FALSE(qr);
}

TEST(URITest, query_remover_remove_current_key_and_value) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "key2=value2&key3=value3");
    qr.remove_current_key_and_value();  /* expected to have not effect */
    qr.remove_current_key_and_value();  /* expected to have not effect */
    ++qr;
    ASSERT_TRUE(qr);
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "key3=value3");
    ++qr;
    ASSERT_TRUE(qr);
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "");
    ++qr;
    ASSERT_FALSE(qr);
}

TEST(URITest, query_remover_random_remove) {
    std::string query = "key1=value1&key2=value2&key3=value3&key4=value4"
                        "&key5=value5&key6=value6";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ++qr;
    ++qr;
    ASSERT_TRUE(qr);
    qr.remove_current_key_and_value();
    ++qr;
    ++qr;
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "key1=value1&key2=value2&key4=value4&key6=value6");
}

TEST(URITest, query_remover_onekey_remove) {
    std::string query = "key1=value1&key2=value2&key3=value3&key4=value4"
                        "&key5=value5&key6=value6";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ++qr;
    ++qr;
    ++qr;
    qr.remove_current_key_and_value();
    ++qr;
    ++qr;
    ASSERT_TRUE(qr);
    ++qr;
    ASSERT_FALSE(qr);
    ++qr;
    ++qr;
    ASSERT_EQ(qr.modified_query(), "key1=value1&key2=value2&key3=value3&key5=value5&key6=value6");
}

TEST(URITest, query_remover_consecutive_ampersand) {
    std::string query = "key1=value1&&&key2=value2&key3=value3&&";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "key2=value2&key3=value3&&");
    ++qr;
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "key3=value3&&");
    qr++;
    qr.remove_current_key_and_value();
    ASSERT_EQ(qr.modified_query(), "");
    ++qr;
    ASSERT_FALSE(qr);
}

TEST(URITest, query_remover_only_equality) {
    std::string query ="key1=&&key2&=&key3=value3";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ASSERT_EQ(qr.key(), "key1");
    ASSERT_EQ(qr.value(), "");
    ++qr;
    ASSERT_EQ(qr.key(), "key2");
    ASSERT_EQ(qr.value(), "");
    ++qr;
    ASSERT_EQ(qr.key(), "");
    ASSERT_EQ(qr.value(), "");
    qr.remove_current_key_and_value();
    ++qr;
    ASSERT_EQ(qr.key(), "key3");
    ASSERT_EQ(qr.value(), "value3");
    ++qr;
    ASSERT_FALSE(qr);
    ASSERT_EQ(qr.modified_query(), "key1=&&key2&key3=value3");
}

TEST(URITest, query_remover_only_one_key) {
    std::string query = "key1";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ASSERT_EQ(qr.key(), "key1");
    ASSERT_EQ(qr.value(), "");
    qr.remove_current_key_and_value();
    ++qr;
    ASSERT_FALSE(qr);
    ASSERT_EQ(qr.modified_query(), "");
}

TEST(URITest, query_remover_no_modify) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ASSERT_EQ(qr.key(), "key1");
    ASSERT_EQ(qr.value(), "value1");
    ++qr;
    ++qr;
    ++qr;
    ASSERT_FALSE(qr);
    ASSERT_EQ(qr.modified_query(), query);
}

TEST(URITest, query_remover_key_value_not_changed_after_modified_query) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(&query);
    ASSERT_TRUE(qr);
    ++qr;
    ASSERT_EQ(qr.key(), "key2");
    ASSERT_EQ(qr.value(), "value2");
    qr.remove_current_key_and_value();
    std::string new_query = qr.modified_query();
    ASSERT_EQ(new_query, "key1=value1&key3=value3");
    ASSERT_EQ(qr.key(), "key2");
    ASSERT_EQ(qr.value(), "value2");
}

TEST(URITest, valid_character) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL("www.baidu2.com':/?#[]@!$&()*+,;=-._~%"));
}
