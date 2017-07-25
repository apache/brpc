// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// File test_uri.cpp
// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date 2014/10/27 14:19:35

#include <gtest/gtest.h>

#include "brpc/uri.h"

namespace brpc {
int ParseQueries(URI::QueryMap& query_map, const std::string &query);
}

TEST(URITest, sanity) {
    brpc::URI uri;
    ASSERT_EQ(0, uri.SetHttpURL("http://user:passwd@www.baidu.com:80/s?wd=uri#frag"));
    ASSERT_EQ("http", uri.schema());
    ASSERT_EQ(80, uri.port());
    ASSERT_EQ("www.baidu.com", uri.host());
    ASSERT_EQ("/s", uri.path());
    ASSERT_EQ("user:passwd", uri.user_info());
    ASSERT_EQ("frag", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri");
    ASSERT_FALSE(uri.GetQuery("nonkey"));

    ASSERT_EQ(0, uri.SetHttpURL("https://www.baidu.com"));
    ASSERT_EQ("https", uri.schema());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu.com", uri.host());
    ASSERT_EQ("", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("", uri.fragment());
    ASSERT_FALSE(uri.GetQuery("wd"));
    ASSERT_FALSE(uri.GetQuery("nonkey"));
    
    ASSERT_EQ(0, uri.SetHttpURL("user:passwd2@www.baidu.com/s?wd=uri2&nonkey=22#frag"));
    ASSERT_EQ("http", uri.schema());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("www.baidu.com", uri.host());
    ASSERT_EQ("/s", uri.path());
    ASSERT_EQ("user:passwd2", uri.user_info());
    ASSERT_EQ("frag", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri2");
    ASSERT_TRUE(uri.GetQuery("nonkey"));
    ASSERT_EQ(*uri.GetQuery("nonkey"), "22");

    // Should work with path as well.
    ASSERT_EQ(0, uri.SetHttpURL("/sb?wd=uri3#frag2"));
    ASSERT_EQ("", uri.schema());
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
    ASSERT_EQ("", uri.schema());
    ASSERT_EQ(-1, uri.port());
    ASSERT_EQ("", uri.host());
    ASSERT_EQ("/x/y/z/", uri.path());
    ASSERT_EQ("", uri.user_info());
    ASSERT_EQ("frag2", uri.fragment());
    ASSERT_TRUE(uri.GetQuery("wd"));
    ASSERT_EQ(*uri.GetQuery("wd"), "uri3");
    ASSERT_FALSE(uri.GetQuery("nonkey"));
}

TEST(URITest,  consecutive_ampersand) {
    const std::string query="&key1=value1&&key3=value3";
    brpc::URI uri;
    ASSERT_EQ(0, brpc::ParseQueries(uri._query_map, query));
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_FALSE(uri.GetQuery("key2"));
    ASSERT_EQ("value1", *uri.GetQuery("key1"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
}

TEST(URITest, only_equality) {
    const std::string query ="key1=&&key2&&=&key3=value3";
    brpc::URI uri;
    ASSERT_EQ(0, brpc::ParseQueries(uri._query_map, query));
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_EQ("", *uri.GetQuery("key1"));
    ASSERT_TRUE(uri.GetQuery("key2"));
    ASSERT_EQ("", *uri.GetQuery("key2"));
    ASSERT_TRUE(uri.GetQuery("key3"));
    ASSERT_EQ("value3", *uri.GetQuery("key3"));
}

TEST(URITest, set_query) {
    const std::string query ="key1=&&key2&&=&key3=value3";
    brpc::URI uri;
    ASSERT_EQ(0, brpc::ParseQueries(uri._query_map, query));
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

TEST(URITest, only_one_key) {
    const std::string query = "key1";
    brpc::URI uri;
    ASSERT_EQ(0, brpc::ParseQueries(uri._query_map, query));
    ASSERT_TRUE(uri.GetQuery("key1"));
    ASSERT_EQ("", *uri.GetQuery("key1"));
}

TEST(URITest, empty_host) {
    brpc::URI uri;
    ASSERT_EQ(-1, uri.SetHttpURL("http://"));
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
    uri.Print(oss, true);
    ASSERT_EQ("http://a.b.c/?d=c&a=b&e=f#frg1", oss.str());
    oss.str("");
    uri.Print(oss, false);
    ASSERT_EQ("/?d=c&a=b&e=f#frg1", oss.str());

    const std::string url2 = "http://a.b.c/?d=c&a=b&e=f#frg1";
    ASSERT_EQ(0, uri.SetHttpURL(url2));
    oss.str("");
    uri.Print(oss, true);
    ASSERT_EQ(url2, oss.str());
    oss.str("");
    uri.Print(oss, false);
    ASSERT_EQ("/?d=c&a=b&e=f#frg1", oss.str());
}

TEST(URITest, copy_and_assign) {
    brpc::URI uri;
    const std::string url = "http://user:passwd@a.b.c/?d=c&a=b&e=f#frg1";
    ASSERT_EQ(0, uri.SetHttpURL(url));
    brpc::URI uri2 = uri;
}

TEST(URITest, query_remover_sanity) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
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
    brpc::QueryRemover qr(query);
    ASSERT_TRUE(qr);
    ASSERT_EQ(qr.key(), "key1");
    ASSERT_EQ(qr.value(), "value1");
    ++qr;
    ++qr;
    ++qr;
    ASSERT_FALSE(qr);
    ASSERT_EQ(qr.modified_query(), "key1=value1&key2=value2&key3=value3");
    // if the query string is not modified, the returned value 
    // should be a reference to the original string
    ASSERT_EQ(qr.modified_query().data(), query.data());
}

TEST(URITest, query_remover_key_value_not_changed_after_modified_query) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    brpc::QueryRemover qr(query);
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

TEST(URITest, query_splitter_sanity) {
    std::string query = "key1=value1&key2=value2&key3=value3";
    {
        brpc::QuerySplitter qs(query);
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key1");
        ASSERT_EQ(qs.value(), "value1");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key2");
        ASSERT_EQ(qs.value(), "value2");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key3");
        ASSERT_EQ(qs.value(), "value3");
        ++qs;
        ASSERT_FALSE(qs);
    }
    {
        brpc::QuerySplitter qs(query.data(), query.data() + query.size());
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key1");
        ASSERT_EQ(qs.value(), "value1");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key2");
        ASSERT_EQ(qs.value(), "value2");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key3");
        ASSERT_EQ(qs.value(), "value3");
        ++qs;
        ASSERT_FALSE(qs);
    }
    {
        brpc::QuerySplitter qs(query.c_str());
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key1");
        ASSERT_EQ(qs.value(), "value1");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key2");
        ASSERT_EQ(qs.value(), "value2");
        ++qs;
        ASSERT_TRUE(qs);
        ASSERT_EQ(qs.key(), "key3");
        ASSERT_EQ(qs.value(), "value3");
        ++qs;
        ASSERT_FALSE(qs);
    }
}
