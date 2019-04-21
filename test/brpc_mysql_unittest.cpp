// Copyright (c) 2019 Baidu, Inc.
// Date: Thu Jun 11 14:30:07 CST 2019


#include <iostream>
#include <sstream>
#include <vector>
#include "butil/time.h"
#include <brpc/mysql.h>
#include <brpc/channel.h>
#include "butil/logging.h"  // LOG()
#include "butil/strings/string_piece.h"
#include <brpc/policy/mysql_authenticator.h>
#include <gtest/gtest.h>

namespace brpc {
const std::string MYSQL_connection_type = "pooled";
const int MYSQL_timeout_ms = 80000;
const int MYSQL_connect_timeout_ms = 80000;

const std::string MYSQL_host = "db4free.net";
const std::string MYSQL_port = "3306";
const std::string MYSQL_user = "brpcuser";
const std::string MYSQL_password = "12345678";
const std::string MYSQL_schema = "brpc_test";
}  // namespace brpc

int main(int argc, char* argv[]) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

namespace {
static pthread_once_t check_mysql_server_once = PTHREAD_ONCE_INIT;

static void CheckMysqlServer() {
    puts("Checking mysql-server...");
    std::stringstream ss;
    ss << "mysql"
       << " -h" << brpc::MYSQL_host << " -P" << brpc::MYSQL_port << " -u" << brpc::MYSQL_user
       << " -p" << brpc::MYSQL_password << " -D" << brpc::MYSQL_schema << " -e 'show databases'";
    puts(ss.str().c_str());
    if (system(ss.str().c_str()) != 0) {
        std::stringstream ss;
        ss << "please startup your mysql-server, then create \nschema:" << brpc::MYSQL_schema
           << "\nuser:" << brpc::MYSQL_user << "\npassword:" << brpc::MYSQL_password;
        puts(ss.str().c_str());
        return;
    }
}

class MysqlTest : public testing::Test {
protected:
    MysqlTest() {}
    void SetUp() {
        pthread_once(&check_mysql_server_once, CheckMysqlServer);
    }
    void TearDown() {}
};

TEST_F(MysqlTest, auth) {
    // config auth
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_MYSQL;
        options.connection_type = brpc::MYSQL_connection_type;
        options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
        options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
        options.auth = new brpc::policy::MysqlAuthenticator(
            brpc::MYSQL_user, brpc::MYSQL_password, brpc::MYSQL_schema);
        std::stringstream ss;
        ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query("show databases");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_RESULTSET, response.reply(0).type());
    }

    // Auth failed
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_MYSQL;
        options.connection_type = brpc::MYSQL_connection_type;
        options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
        options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
        options.auth =
            new brpc::policy::MysqlAuthenticator(brpc::MYSQL_user, "123456789", brpc::MYSQL_schema);
        std::stringstream ss;
        ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query("show databases");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_TRUE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(brpc::MYSQL_RSP_UNKNOWN, response.reply(0).type());
    }

    // check noauth.
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_MYSQL;
        options.connection_type = brpc::MYSQL_connection_type;
        options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
        options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
        std::stringstream ss;
        ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query("show databases");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_TRUE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(brpc::MYSQL_RSP_UNKNOWN, response.reply(0).type());
    }
}

TEST_F(MysqlTest, ok) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = brpc::MYSQL_connection_type;
    options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
    options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
    options.auth = new brpc::policy::MysqlAuthenticator(
        brpc::MYSQL_user, brpc::MYSQL_password, brpc::MYSQL_schema);
    std::stringstream ss;
    ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("drop table brpc_table");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    }
    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query(
            "CREATE TABLE IF NOT EXISTS `brpc_table` (`col1` int(11) NOT NULL AUTO_INCREMENT, "
            "`col2` varchar(45) DEFAULT NULL, "
            "`col3` decimal(6,3) DEFAULT NULL, `col4` datetime DEFAULT NULL, `col5` blob, `col6` "
            "binary(6) DEFAULT NULL, `col7` tinyblob, `col8` longblob, `col9` mediumblob, `col10` "
            "tinyblob, `col11` varbinary(10) DEFAULT NULL, `col12` date DEFAULT NULL, `col13` "
            "datetime(6) DEFAULT NULL, `col14` time DEFAULT NULL, `col15` timestamp(4) NULL "
            "DEFAULT NULL, `col16` year(4) DEFAULT NULL, `col17` geometry DEFAULT NULL, `col18` "
            "geometrycollection DEFAULT NULL, `col19` linestring DEFAULT NULL, `col20` point "
            "DEFAULT NULL, `col21` polygon DEFAULT NULL, `col22` bigint(64) DEFAULT NULL, `col23` "
            "decimal(10,0) DEFAULT NULL, `col24` double DEFAULT NULL, `col25` float DEFAULT NULL, "
            "`col26` int(7) DEFAULT NULL, `col27` mediumint(18) DEFAULT NULL, `col28` double "
            "DEFAULT NULL, `col29` smallint(2) DEFAULT NULL, `col30` tinyint(1) DEFAULT NULL, "
            "`col31` char(6) DEFAULT NULL, `col32` varchar(6) DEFAULT NULL, `col33` longtext, "
            "`col34` mediumtext, `col35` tinytext, `col36` tinytext, `col37` bit(7) DEFAULT NULL, "
            "`col38` tinyint(4) DEFAULT NULL, `col39` varchar(45) DEFAULT NULL, `col40` "
            "varchar(45) CHARACTER SET utf8 DEFAULT NULL, `col41` char(4) CHARACTER SET utf8 "
            "DEFAULT NULL, `col42` varchar(6) CHARACTER SET utf8 DEFAULT NULL, PRIMARY KEY "
            "(`col1`)) ENGINE=InnoDB AUTO_INCREMENT=1157 DEFAULT CHARSET=utf8");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        std::stringstream ss1;
        ss1 << "INSERT INTO `brpc_table` "
               "(`col2`,`col3`,`col4`,`col5`,`col6`,`col7`,`col8`,`col9`,`col10`,`col11`,`"
               "col12`,`col13`,`col14`,`col15`,`col16`,`col17`,`col18`,`col19`,`col20`,`col21`, "
               "`col22` "
               ",`col23`,`col24`,`col25`,`col26`,`col27`,`col28`,`col29`,`col30`,`col31`,`col32`,`"
               "col33`,`col34`,`col35`,`col36`,`col37`,`col38`,`col39`,`col40`,`col41`,`col42`) "
               "VALUES ('col2',0.015,'2018-12-01 "
               "12:13:14','aaa','bbb','ccc','ddd','eee','fff','ggg','2014-09-18', '2010-12-10 "
               "14:12:09.019473' ,'01:06:09','1970-01-01 08:00:00.9856' "
               ",2014,NULL,NULL,NULL,NULL,NULL,69,'12.5',16.9,6.7,24,37,69.56,234,6, '"
               "col31','col32','col33','col34','col35','col36',NULL,9,'col39','col40','col4' ,'"
               "col42')";
        request.Query(ss1.str());
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
    }
}

TEST_F(MysqlTest, error) {
    {
        brpc::ChannelOptions options;
        options.protocol = brpc::PROTOCOL_MYSQL;
        options.connection_type = brpc::MYSQL_connection_type;
        options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
        options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
        options.auth = new brpc::policy::MysqlAuthenticator(
            brpc::MYSQL_user, brpc::MYSQL_password, brpc::MYSQL_schema);
        std::stringstream ss;
        ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
        brpc::Channel channel;
        ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query("select nocol from notable");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_ERROR, response.reply(0).type());
    }
}

TEST_F(MysqlTest, resultset) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = brpc::MYSQL_connection_type;
    options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
    options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
    options.auth = new brpc::policy::MysqlAuthenticator(
        brpc::MYSQL_user, brpc::MYSQL_password, brpc::MYSQL_schema, "charset=utf8");
    std::stringstream ss;
    ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));
    {
        for (int i = 0; i < 50; ++i) {
            brpc::MysqlRequest request;
            brpc::MysqlResponse response;
            brpc::Controller cntl;

            std::stringstream ss1;
            ss1 << "INSERT INTO `brpc_table` "
                   "(`col2`,`col3`,`col4`,`col5`,`col6`,`col7`,`col8`,`col9`,`col10`,`col11`"
                   ",`"
                   "col12`,`col13`,`col14`,`col15`,`col16`,`col17`,`col18`,`col19`,`col20`,`col21`,"
                   " "
                   "`col22` "
                   ",`col23`,`col24`,`col25`,`col26`,`col27`,`col28`,`col29`,`col30`,`col31`,`"
                   "col32`,`"
                   "col33`,`col34`,`col35`,`col36`,`col37`,`col38`,`col39`,`col40`,`col41`,`col42`)"
                   " VALUES ('col2',0.015,'2018-12-01 "
                   "12:13:14','aaa','bbb','ccc','ddd','eee','fff','ggg','2014-09-18', '2010-12-10 "
                   "14:12:09.019473' ,'01:06:09','1970-01-01 08:00:00.9856' "
                   ",2014,NULL,NULL,NULL,NULL,NULL,69,'12.5',16.9,6.7,24,37,69.56,234,6, '"
                   "col31','col32','col33','col34','col35','col36',NULL,9,'col39','col40','col4' "
                   ",'"
                   "col42')";
            request.Query(ss1.str());
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
        }
    }

    {
        std::stringstream ss1;
        for (int i = 0; i < 30; ++i) {
            ss1 << "INSERT INTO `brpc_table` "
                   "(`col2`,`col3`,`col4`,`col5`,`col6`,`col7`,`col8`,`col9`,`col10`,`col11`"
                   ",`"
                   "col12`,`col13`,`col14`,`col15`,`col16`,`col17`,`col18`,`col19`,`col20`,`col21`,"
                   " "
                   "`col22` "
                   ",`col23`,`col24`,`col25`,`col26`,`col27`,`col28`,`col29`,`col30`,`col31`,`"
                   "col32`,`"
                   "col33`,`col34`,`col35`,`col36`,`col37`,`col38`,`col39`,`col40`,`col41`,`col42`)"
                   "VALUES ('col2',0.015,'2018-12-01 "
                   "12:13:14','aaa','bbb','ccc','ddd','eee','fff','ggg','2014-09-18', '2010-12-10 "
                   "14:12:09.019473' ,'01:06:09','1970-01-01 08:00:00.9856' "
                   ",2014,NULL,NULL,NULL,NULL,NULL,69,'12.5',16.9,6.7,24,37,69.56,234,6, '"
                   "col31','col32','col33','col34','col35','col36',NULL,9,'col39','col40','col4' "
                   ",'"
                   "col42');";
        }
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query(ss1.str());
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(30ul, response.reply_size());
        for (int i = 0; i < 30; ++i) {
            ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(i).type());
        }
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("select count(0) from brpc_table");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        // ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_RESULTSET, response.reply(0).type());
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("select * from brpc_table where 1 = 2");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_RESULTSET, response.reply(0).type());
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("select * from brpc_table limit 10");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_RESULTSET, response.reply(0).type());
        ASSERT_EQ(42ull, response.reply(0).column_number());
        const brpc::MysqlReply& reply = response.reply(0);
        ASSERT_EQ(reply.column(0).name(), "col1");
        ASSERT_EQ(reply.column(0).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(0).type(), brpc::MYSQL_FIELD_TYPE_LONG);

        ASSERT_EQ(reply.column(1).name(), "col2");
        ASSERT_EQ(reply.column(1).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(1).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        ASSERT_EQ(reply.column(2).name(), "col3");
        ASSERT_EQ(reply.column(2).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(2).type(), brpc::MYSQL_FIELD_TYPE_NEWDECIMAL);

        ASSERT_EQ(reply.column(3).name(), "col4");
        ASSERT_EQ(reply.column(3).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(3).type(), brpc::MYSQL_FIELD_TYPE_DATETIME);

        ASSERT_EQ(reply.column(4).name(), "col5");
        ASSERT_EQ(reply.column(4).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(4).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(5).name(), "col6");
        ASSERT_EQ(reply.column(5).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(5).type(), brpc::MYSQL_FIELD_TYPE_STRING);

        ASSERT_EQ(reply.column(6).name(), "col7");
        ASSERT_EQ(reply.column(6).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(6).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(7).name(), "col8");
        ASSERT_EQ(reply.column(7).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(7).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(8).name(), "col9");
        ASSERT_EQ(reply.column(8).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(8).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(9).name(), "col10");
        ASSERT_EQ(reply.column(9).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(9).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(10).name(), "col11");
        ASSERT_EQ(reply.column(10).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(10).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        ASSERT_EQ(reply.column(11).name(), "col12");
        ASSERT_EQ(reply.column(11).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(11).type(), brpc::MYSQL_FIELD_TYPE_DATE);

        ASSERT_EQ(reply.column(12).name(), "col13");
        ASSERT_EQ(reply.column(12).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(12).type(), brpc::MYSQL_FIELD_TYPE_DATETIME);

        ASSERT_EQ(reply.column(13).name(), "col14");
        ASSERT_EQ(reply.column(13).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(13).type(), brpc::MYSQL_FIELD_TYPE_TIME);

        ASSERT_EQ(reply.column(14).name(), "col15");
        ASSERT_EQ(reply.column(14).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(14).type(), brpc::MYSQL_FIELD_TYPE_TIMESTAMP);

        ASSERT_EQ(reply.column(15).name(), "col16");
        ASSERT_EQ(reply.column(15).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(15).type(), brpc::MYSQL_FIELD_TYPE_YEAR);

        ASSERT_EQ(reply.column(16).name(), "col17");
        ASSERT_EQ(reply.column(16).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(16).type(), brpc::MYSQL_FIELD_TYPE_GEOMETRY);

        ASSERT_EQ(reply.column(17).name(), "col18");
        ASSERT_EQ(reply.column(17).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(17).type(), brpc::MYSQL_FIELD_TYPE_GEOMETRY);

        ASSERT_EQ(reply.column(18).name(), "col19");
        ASSERT_EQ(reply.column(18).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(18).type(), brpc::MYSQL_FIELD_TYPE_GEOMETRY);

        ASSERT_EQ(reply.column(19).name(), "col20");
        ASSERT_EQ(reply.column(19).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(19).type(), brpc::MYSQL_FIELD_TYPE_GEOMETRY);

        ASSERT_EQ(reply.column(20).name(), "col21");
        ASSERT_EQ(reply.column(20).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(20).type(), brpc::MYSQL_FIELD_TYPE_GEOMETRY);

        ASSERT_EQ(reply.column(21).name(), "col22");
        ASSERT_EQ(reply.column(21).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(21).type(), brpc::MYSQL_FIELD_TYPE_LONGLONG);

        ASSERT_EQ(reply.column(22).name(), "col23");
        ASSERT_EQ(reply.column(22).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(22).type(), brpc::MYSQL_FIELD_TYPE_NEWDECIMAL);

        ASSERT_EQ(reply.column(23).name(), "col24");
        ASSERT_EQ(reply.column(23).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(23).type(), brpc::MYSQL_FIELD_TYPE_DOUBLE);

        ASSERT_EQ(reply.column(24).name(), "col25");
        ASSERT_EQ(reply.column(24).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(24).type(), brpc::MYSQL_FIELD_TYPE_FLOAT);

        ASSERT_EQ(reply.column(25).name(), "col26");
        ASSERT_EQ(reply.column(25).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(25).type(), brpc::MYSQL_FIELD_TYPE_LONG);

        ASSERT_EQ(reply.column(26).name(), "col27");
        ASSERT_EQ(reply.column(26).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(26).type(), brpc::MYSQL_FIELD_TYPE_INT24);

        ASSERT_EQ(reply.column(27).name(), "col28");
        ASSERT_EQ(reply.column(27).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(27).type(), brpc::MYSQL_FIELD_TYPE_DOUBLE);

        ASSERT_EQ(reply.column(28).name(), "col29");
        ASSERT_EQ(reply.column(28).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(28).type(), brpc::MYSQL_FIELD_TYPE_SHORT);

        ASSERT_EQ(reply.column(29).name(), "col30");
        ASSERT_EQ(reply.column(29).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(29).type(), brpc::MYSQL_FIELD_TYPE_TINY);

        ASSERT_EQ(reply.column(30).name(), "col31");
        ASSERT_EQ(reply.column(30).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(30).type(), brpc::MYSQL_FIELD_TYPE_STRING);

        ASSERT_EQ(reply.column(31).name(), "col32");
        ASSERT_EQ(reply.column(31).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(31).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        ASSERT_EQ(reply.column(32).name(), "col33");
        ASSERT_EQ(reply.column(32).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(32).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(33).name(), "col34");
        ASSERT_EQ(reply.column(33).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(33).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(34).name(), "col35");
        ASSERT_EQ(reply.column(34).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(34).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(35).name(), "col36");
        ASSERT_EQ(reply.column(35).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(35).type(), brpc::MYSQL_FIELD_TYPE_BLOB);

        ASSERT_EQ(reply.column(36).name(), "col37");
        ASSERT_EQ(reply.column(36).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(36).type(), brpc::MYSQL_FIELD_TYPE_BIT);

        ASSERT_EQ(reply.column(37).name(), "col38");
        ASSERT_EQ(reply.column(37).collation(), brpc::MYSQL_binary);
        ASSERT_EQ(reply.column(37).type(), brpc::MYSQL_FIELD_TYPE_TINY);

        ASSERT_EQ(reply.column(38).name(), "col39");
        ASSERT_EQ(reply.column(38).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(38).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        ASSERT_EQ(reply.column(39).name(), "col40");
        ASSERT_EQ(reply.column(39).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(39).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        ASSERT_EQ(reply.column(40).name(), "col41");
        ASSERT_EQ(reply.column(40).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(40).type(), brpc::MYSQL_FIELD_TYPE_STRING);

        ASSERT_EQ(reply.column(41).name(), "col42");
        ASSERT_EQ(reply.column(41).collation(), brpc::MYSQL_utf8_general_ci);
        ASSERT_EQ(reply.column(41).type(), brpc::MYSQL_FIELD_TYPE_VAR_STRING);

        for (uint64_t idx = 0; idx < reply.row_number(); ++idx) {
            const brpc::MysqlReply::Row& row = reply.next();
            ASSERT_EQ(row.field(1).string(), "col2");
            ASSERT_EQ(row.field(2).string(), "0.015");
            ASSERT_EQ(row.field(3).string(), "2018-12-01 12:13:14");
            ASSERT_EQ(row.field(4).string(), "aaa");
            butil::StringPiece field5 = row.field(5).string();
            ASSERT_EQ(field5.size(), size_t(6));
            ASSERT_EQ(field5[0], 'b');
            ASSERT_EQ(field5[1], 'b');
            ASSERT_EQ(field5[2], 'b');
            ASSERT_EQ(field5[3], '\0');
            ASSERT_EQ(field5[4], '\0');
            ASSERT_EQ(field5[5], '\0');
            ASSERT_EQ(row.field(6).string(), "ccc");
            ASSERT_EQ(row.field(7).string(), "ddd");
            ASSERT_EQ(row.field(8).string(), "eee");
            ASSERT_EQ(row.field(9).string(), "fff");
            ASSERT_EQ(row.field(10).string(), "ggg");
            ASSERT_EQ(row.field(11).string(), "2014-09-18");
            ASSERT_EQ(row.field(12).string(), "2010-12-10 14:12:09.019473");
            ASSERT_EQ(row.field(13).string(), "01:06:09");
            ASSERT_EQ(row.field(14).string(), "1970-01-01 08:00:00.9856");
            ASSERT_EQ(row.field(15).small(), uint16_t(2014));
            ASSERT_EQ(row.field(16).is_nil(), true);
            ASSERT_EQ(row.field(17).is_nil(), true);
            ASSERT_EQ(row.field(18).is_nil(), true);
            ASSERT_EQ(row.field(19).is_nil(), true);
            ASSERT_EQ(row.field(20).is_nil(), true);
            ASSERT_EQ(row.field(21).sbigint(), int64_t(69));
            ASSERT_EQ(row.field(22).string(), "13");
            ASSERT_EQ(row.field(23).float64(), double(16.9));
            ASSERT_EQ(row.field(24).float32(), float(6.7));
            ASSERT_EQ(row.field(25).sinteger(), int32_t(24));
            ASSERT_EQ(row.field(26).sinteger(), int32_t(37));
            ASSERT_EQ(row.field(27).float64(), double(69.56));
            ASSERT_EQ(row.field(28).ssmall(), int16_t(234));
            ASSERT_EQ(row.field(29).stiny(), '6');
            ASSERT_EQ(row.field(30).string(), "col31");
            ASSERT_EQ(row.field(31).string(), "col32");
            ASSERT_EQ(row.field(32).string(), "col33");
            ASSERT_EQ(row.field(33).string(), "col34");
            ASSERT_EQ(row.field(34).string(), "col35");
            ASSERT_EQ(row.field(35).string(), "col36");
            ASSERT_EQ(row.field(36).is_nil(), true);
            ASSERT_EQ(row.field(37).stiny(), '9');
            ASSERT_EQ(row.field(38).string(), "col39");
            ASSERT_EQ(row.field(39).string(), "col40");
            ASSERT_EQ(row.field(40).string(), "col4");  // size is 4
            ASSERT_EQ(row.field(41).string(), "col42");
        }
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("delete from brpc_table");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        // ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
    }

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("drop table brpc_table");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        // ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
    }
}

TEST_F(MysqlTest, transaction) {
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_MYSQL;
    options.connection_type = brpc::MYSQL_connection_type;
    options.connect_timeout_ms = brpc::MYSQL_connect_timeout_ms;
    options.timeout_ms = brpc::MYSQL_timeout_ms /*milliseconds*/;
    options.auth = new brpc::policy::MysqlAuthenticator(
        brpc::MYSQL_user, brpc::MYSQL_password, brpc::MYSQL_schema);
    std::stringstream ss;
    ss << brpc::MYSQL_host + ":" + brpc::MYSQL_port;
    brpc::Channel channel;
    ASSERT_EQ(0, channel.Init(ss.str().c_str(), &options));

    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;
        request.Query("drop table brpc_tx");
        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
    }
    {
        brpc::MysqlRequest request;
        brpc::MysqlResponse response;
        brpc::Controller cntl;

        request.Query(
            "CREATE TABLE IF NOT EXISTS `brpc_tx` (`Id` int(11) NOT NULL AUTO_INCREMENT,`LastName` "
            "varchar(255) DEFAULT "
            "NULL,`FirstName` decimal(10,0) DEFAULT NULL,`Address` varchar(255) DEFAULT "
            "NULL,`City` varchar(255) DEFAULT NULL, PRIMARY KEY (`Id`)) ENGINE=InnoDB "
            "AUTO_INCREMENT=1157 DEFAULT CHARSET=utf8");

        channel.CallMethod(NULL, &cntl, &request, &response, NULL);
        ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
        ASSERT_EQ(1ul, response.reply_size());
        ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
    }
    {
        brpc::MysqlTransactionOptions tx_options;
        tx_options.readonly = false;
        tx_options.isolation_level = brpc::MysqlIsoRepeatableRead;
        brpc::MysqlTransactionUniquePtr tx(brpc::NewMysqlTransaction(channel, tx_options));
        ASSERT_FALSE(tx == NULL) << "Fail to create transaction";
        uint64_t idx1, idx2;
        {
            brpc::MysqlRequest request(tx.get());
            std::string sql =
                "insert into brpc_tx(LastName,FirstName, Address) values "
                "('lucy',12.5,'beijing')";
            ASSERT_EQ(request.Query(sql), true);
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
            idx1 = response.reply(0).ok().index();
        }
        {
            brpc::MysqlRequest request(tx.get());
            std::string sql =
                "insert into brpc_tx(LastName,FirstName, Address) values "
                "('lilei',12.6,'shanghai')";
            ASSERT_EQ(request.Query(sql), true);
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(brpc::MYSQL_RSP_OK, response.reply(0).type());
            idx2 = response.reply(0).ok().index();
        }

        LOG(INFO) << "idx1=" << idx1 << " idx2=" << idx2;
        // not commit, so return 0 rows
        {
            brpc::MysqlRequest request;
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            std::stringstream ss;
            ss << "select * from brpc_tx where id in (" << idx1 << "," << idx2 << ")";
            request.Query(ss.str());
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(response.reply(0).row_number(), 0ul);
        }

        { ASSERT_EQ(tx->commit(), true); }
        // after commit, so return 2 rows
        {
            brpc::MysqlRequest request;
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            std::stringstream ss;
            ss << "select * from brpc_tx where id in (" << idx1 << "," << idx2 << ")";
            request.Query(ss.str());
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(response.reply(0).row_number(), 2ul);
        }
    }

    {
        brpc::MysqlTransactionOptions tx_options;
        tx_options.readonly = true;
        tx_options.isolation_level = brpc::MysqlIsoReadCommitted;

        brpc::MysqlTransactionUniquePtr tx(brpc::NewMysqlTransaction(channel, tx_options));
        ASSERT_FALSE(tx == NULL) << "Fail to create transaction";

        {
            brpc::MysqlRequest request(tx.get());
            std::string sql = "update brpc_tx set Address = 'hangzhou' where Id=1";
            ASSERT_EQ(request.Query(sql), true);
            brpc::MysqlResponse response;
            brpc::Controller cntl;
            channel.CallMethod(NULL, &cntl, &request, &response, NULL);
            ASSERT_FALSE(cntl.Failed()) << cntl.ErrorText();
            ASSERT_EQ(1ul, response.reply_size());
            ASSERT_EQ(brpc::MYSQL_RSP_ERROR, response.reply(0).type());
        }
    }
}

}  // namespace
