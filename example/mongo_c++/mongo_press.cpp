// Copyright (c) 2014 Baidu, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// A brpc based test talk with mongodb server

#include <brpc/channel.h>
#include <butil/logging.h>
#include <gflags/gflags.h>

#include <sstream>
// #include <brpc/mongo.h>
#include <brpc/policy/mongo_authenticator.h>
#include <brpc/server.h>
#include <bson/bson.h>
#include <bthread/bthread.h>
#include <butil/bson_util.h>
#include <bvar/bvar.h>

#include "brpc/mongo.h"

DEFINE_string(connection_type, "pooled",
              "Connection type. Available values: pooled, short");
DEFINE_string(server, "127.0.0.1", "IP Address of server");
DEFINE_int32(port, 27017, "Port of server");
DEFINE_string(user, "brpcuser", "user name");
DEFINE_string(password, "12345678", "password");
DEFINE_string(database, "test", "database");
DEFINE_string(collection, "people", "collection");
// DEFINE_string(data, "ABCDEF", "data");
DEFINE_int32(timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(connect_timeout_ms, 5000, "RPC timeout in milliseconds");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_int32(thread_num, 1, "Number of threads to send requests");
DEFINE_bool(use_bthread, true, "Use bthread to send requests");
DEFINE_int32(dummy_port, -1, "port of dummy server(for monitoring)");
DEFINE_int32(op_type, 1,
             "CRUD operation, 0:INSERT, 1:SELECT, 2:UPDATE, 3:COUNT, 4:DELETE");
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");

struct SenderArgs {
  int base_index;
  brpc::Channel* mongo_channel;
};

std::pair<bool, brpc::MongoQueryResponse> get_more(brpc::Channel* channel,
                                                   int64_t cursorid) {
  brpc::MongoGetMoreRequest getMore_request;
  getMore_request.set_database(FLAGS_database);
  getMore_request.set_collection(FLAGS_collection);
  getMore_request.set_cursorid(cursorid);
  getMore_request.set_batch_size(100);
  brpc::Controller cntl;
  brpc::MongoQueryResponse query_response;
  channel->CallMethod(NULL, &cntl, &getMore_request, &query_response, NULL);
  if (!cntl.Failed()) {
    return std::make_pair(true, query_response);
  } else {
    LOG(ERROR) << "error=" << cntl.ErrorText();
    return std::make_pair(false, query_response);
  }
}

static void* delete_test(void* void_args) {
  SenderArgs* args = (SenderArgs*)void_args;
  brpc::Channel* mongo_channel = args->mongo_channel;

  // insert then delete
  brpc::MongoInsertRequest insert_request;
  brpc::MongoDeleteRequest delete_request;
  while (!brpc::IsAskedToQuit()) {
    // insert random number records
    insert_request.Clear();
    insert_request.set_database(FLAGS_database);
    insert_request.set_collection(FLAGS_collection);
    size_t number = random() % 10 + 1;
    for (size_t i = 0; i < number; ++i) {
      butil::bson::BsonPtr doc = butil::bson::new_bson();
      std::string name = "test_" + std::to_string(i);
      BSON_APPEND_UTF8(doc.get(), "name", name.c_str());
      BSON_APPEND_UTF8(doc.get(), "comment", "delete_test");
      insert_request.add_documents(doc);
    }
    brpc::Controller insert_cntl;
    brpc::MongoInsertResponse insert_response;
    mongo_channel->CallMethod(nullptr, &insert_cntl, &insert_request,
                              &insert_response, nullptr);
    if (!insert_cntl.Failed()) {
      if (insert_response.number() != number) {
        std::stringstream ss;
        for (size_t i = 0; i < insert_response.write_errors_size(); ++i) {
          ss << insert_response.write_errors(i).errmsg;
        }
        LOG(INFO) << "insert failed, errmsg:" << ss.str();
        break;
      } else {
        LOG(INFO) << "insert succ, number:" << number;
      }
    } else {
      LOG(INFO) << "insert failed, error=" << insert_cntl.ErrorText();
      break;
    }
    // delete records
    delete_request.Clear();
    delete_request.set_database(FLAGS_database);
    delete_request.set_collection(FLAGS_collection);
    delete_request.set_delete_many(true);
    butil::bson::BsonPtr delete_filter = butil::bson::new_bson();
    BSON_APPEND_UTF8(delete_filter.get(), "comment", "delete_test");
    delete_request.set_query(delete_filter);
    brpc::Controller delete_cntl;
    brpc::MongoDeleteResponse delete_response;
    mongo_channel->CallMethod(nullptr, &delete_cntl, &delete_request,
                              &delete_response, nullptr);
    if (!delete_cntl.Failed()) {
      if (delete_response.number() != number) {
        LOG(INFO) << "delete failed, expect:" << number
                  << " actual:" << delete_response.number();
        break;
      } else {
        LOG(INFO) << "delete succ, number:" << number;
      }
    } else {
      LOG(INFO) << "delete failed, error=" << delete_cntl.ErrorText();
      break;
    }
  }
}

static void* update_test(void* void_args) {
  SenderArgs* args = (SenderArgs*)void_args;
  brpc::Channel* mongo_channel = args->mongo_channel;

  // insert then update
  brpc::MongoInsertRequest insert_request;
  insert_request.set_database(FLAGS_database);
  insert_request.set_collection(FLAGS_collection);
  size_t number = random() % 10 + 1;
  for (size_t i = 0; i < number; ++i) {
    butil::bson::BsonPtr doc = butil::bson::new_bson();
    std::string name = "test_" + std::to_string(i);
    BSON_APPEND_UTF8(doc.get(), "name", name.c_str());
    BSON_APPEND_UTF8(doc.get(), "comment", "update_test");
    insert_request.add_documents(doc);
  }
  brpc::Controller insert_cntl;
  brpc::MongoInsertResponse insert_response;
  mongo_channel->CallMethod(nullptr, &insert_cntl, &insert_request,
                            &insert_response, nullptr);
  if (!insert_cntl.Failed()) {
    if (insert_response.number() != number) {
      std::stringstream ss;
      for (size_t i = 0; i < insert_response.write_errors_size(); ++i) {
        ss << insert_response.write_errors(i).errmsg;
      }
      LOG(INFO) << "insert failed, errmsg:" << ss.str();
      return nullptr;
    } else {
      LOG(INFO) << "insert succ, number:" << number;
    }
  } else {
    LOG(INFO) << "insert failed, error=" << insert_cntl.ErrorText();
    return nullptr;
  }
  // test upsert
  brpc::MongoUpdateRequest upsert_request;
  upsert_request.set_database(FLAGS_database);
  upsert_request.set_collection(FLAGS_collection);
  upsert_request.set_upsert(true);
  butil::bson::BsonPtr selector = butil::bson::new_bson();
  BSON_APPEND_UTF8(selector.get(), "comment", "upsert_test");
  upsert_request.set_selector(selector);
  butil::bson::BsonPtr update_doc = butil::bson::new_bson();
  BSON_APPEND_INT32(update_doc.get(), "time", time(nullptr));
  butil::bson::BsonPtr update = butil::bson::new_bson();
  BSON_APPEND_DOCUMENT(update.get(), "$set", update_doc.get());
  upsert_request.set_update(update);
  brpc::Controller update_cntl;
  brpc::MongoUpdateResponse update_response;
  mongo_channel->CallMethod(nullptr, &update_cntl, &upsert_request,
                            &update_response, nullptr);
  if (!update_cntl.Failed()) {
    std::stringstream ss;
    if (update_response.upserted_docs_size()) {
      for (size_t i = 0; i < update_response.upserted_docs_size(); ++i) {
        const brpc::UpsertedDoc doc = update_response.upserted_docs(i);
        char oid_str[25];
        bson_oid_to_string(&doc._id, oid_str);
        ss << "index:" << doc.index << " _id:" << oid_str << "\n";
      }
    }
    LOG(INFO) << "upsert match num:" << update_response.matched_number()
              << ", modify num:" << update_response.modified_number()
              << ss.str();
  } else {
    LOG(INFO) << "update failed, error=" << update_cntl.ErrorText();
    return nullptr;
  }
  brpc::MongoUpdateRequest update_request;
  while (!brpc::IsAskedToQuit()) {
    // update records
    update_request.Clear();
    update_request.set_database(FLAGS_database);
    update_request.set_collection(FLAGS_collection);
    update_request.set_ordered(true);
    update_request.set_multi(true);
    butil::bson::BsonPtr selector = butil::bson::new_bson();
    BSON_APPEND_UTF8(selector.get(), "comment", "update_test");
    update_request.set_selector(selector);
    butil::bson::BsonPtr update_doc = butil::bson::new_bson();
    BSON_APPEND_INT32(update_doc.get(), "time", time(nullptr));
    butil::bson::BsonPtr update = butil::bson::new_bson();
    BSON_APPEND_DOCUMENT(update.get(), "$set", update_doc.get());
    update_request.set_update(update);
    brpc::Controller update_cntl;
    brpc::MongoUpdateResponse update_response;
    mongo_channel->CallMethod(nullptr, &update_cntl, &update_request,
                              &update_response, nullptr);
    if (!update_cntl.Failed()) {
      if (update_response.modified_number() !=
          update_response.matched_number()) {
        LOG(INFO) << "update failed, matched number:"
                  << update_response.matched_number()
                  << ", modify number:" << update_response.modified_number();
        break;
      } else {
        LOG(INFO) << "update succ, match_number:"
                  << update_response.matched_number()
                  << ", modify number:" << update_response.modified_number();
      }
    } else {
      LOG(INFO) << "update failed, error=" << update_cntl.ErrorText();
      break;
    }
    bthread_usleep(2 * 1000 * 1000);
  }
}

// Send `command' to mongo-server via `channel'
static void* sender(void* void_args) {
  SenderArgs* args = (SenderArgs*)void_args;

  google::protobuf::Message* request = nullptr;
  if (FLAGS_op_type == 0) {
    // insert
    // brpc::MongoInsertRequest *insert_request = new
    // brpc::MongoInsertRequest(); insert_request->set_database(FLAGS_database);
    // insert_request->set_collection(FLAGS_collection);
    // butil::bson::BsonPtr doc1 = butil::bson::new_bson();
    // BSON_APPEND_UTF8(doc1.get(), "name", "test2");
    // BSON_APPEND_UTF8(doc1.get(), "comment", "insert2");
    // insert_request->add_documents(doc1);
    // request = insert_request;
  } else if (FLAGS_op_type == 1) {
    // query
    brpc::MongoQueryRequest* query_request = new brpc::MongoQueryRequest();
    query_request->set_database(FLAGS_database);
    query_request->set_collection(FLAGS_collection);
    // query_request->set_limit(10);
    request = query_request;
  } else if (FLAGS_op_type == 2) {
    // update

  } else if (FLAGS_op_type == 3) {
    // count
    brpc::MongoCountRequest* count_request = new brpc::MongoCountRequest();
    count_request->set_database(FLAGS_database);
    count_request->set_collection(FLAGS_collection);
    request = count_request;
  }

  while (!brpc::IsAskedToQuit()) {
    google::protobuf::Message* response = nullptr;
    brpc::Controller cntl;
    if (FLAGS_op_type == 0) {
      brpc::MongoInsertRequest* insert_request = new brpc::MongoInsertRequest();
      insert_request->set_database(FLAGS_database);
      insert_request->set_collection(FLAGS_collection);
      butil::bson::BsonPtr doc1 = butil::bson::new_bson();
      BSON_APPEND_UTF8(doc1.get(), "name", "test1");
      BSON_APPEND_UTF8(doc1.get(), "comment", "insert1");
      butil::bson::BsonPtr doc2 = butil::bson::new_bson();
      BSON_APPEND_UTF8(doc2.get(), "name", "test2");
      BSON_APPEND_UTF8(doc2.get(), "comment", "insert2");
      insert_request->add_documents(doc1);
      insert_request->add_documents(doc2);
      request = insert_request;
      response = new brpc::MongoInsertResponse();
    } else if (FLAGS_op_type == 1) {
      response = new brpc::MongoQueryResponse();
    } else if (FLAGS_op_type == 2) {
    } else if (FLAGS_op_type == 3) {
      response = new brpc::MongoCountResponse();
    }

    const int64_t elp = cntl.latency_us();
    args->mongo_channel->CallMethod(NULL, &cntl, request, response, NULL);
    if (!cntl.Failed()) {
      if (FLAGS_op_type == 0) {
        brpc::MongoInsertResponse* insert_response =
            dynamic_cast<brpc::MongoInsertResponse*>(response);
        LOG(INFO) << "insert return num:" << insert_response->number()
                  << " write_errors num:"
                  << insert_response->write_errors().size();
        for (size_t i = 0; i < insert_response->write_errors().size(); ++i) {
          brpc::WriteError write_error = insert_response->write_errors(i);
          LOG(INFO) << "index:" << write_error.index
                    << " code:" << write_error.code
                    << " errmsg:" << write_error.errmsg;
        }
      } else if (FLAGS_op_type == 1) {
        brpc::MongoQueryResponse* query_response =
            dynamic_cast<brpc::MongoQueryResponse*>(response);
        assert(query_response);
        LOG(INFO) << "query return num:" << query_response->number_returned();
        LOG(INFO) << "query return document num:"
                  << query_response->documents().size();
        LOG_IF(INFO, query_response->has_cursorid())
            << "cursorid:" << query_response->cursorid();
        int64_t cursor_id = 0;
        if (query_response->has_cursorid()) {
          cursor_id = query_response->cursorid();
        }
        while (cursor_id) {
          std::pair<bool, brpc::MongoQueryResponse> getMore_result =
              get_more(args->mongo_channel, cursor_id);
          if (getMore_result.first) {
            auto& getMore_response = getMore_result.second;
            // 返回成功
            LOG(INFO) << "query return num:"
                      << getMore_response.number_returned();
            LOG(INFO) << "query return document num:"
                      << getMore_response.documents().size();
            LOG_IF(INFO, getMore_response.has_cursorid())
                << "cursorid:" << getMore_response.cursorid();
            if (getMore_response.has_cursorid()) {
              cursor_id = getMore_response.cursorid();
            } else {
              cursor_id = 0;
            }
          } else {
            cursor_id = 0;
          }
        }
      } else if (FLAGS_op_type == 2) {
      } else if (FLAGS_op_type == 3) {
        brpc::MongoCountResponse* count_response =
            dynamic_cast<brpc::MongoCountResponse*>(response);
        assert(count_response);
        LOG(INFO) << "count return num:" << count_response->number();
      }
    } else {
      g_error_count << 1;
      CHECK(brpc::IsAskedToQuit() || !FLAGS_dont_fail)
          << "error=" << cntl.ErrorText() << " latency=" << elp;
      // We can't connect to the server, sleep a while. Notice that this
      // is a specific sleeping to prevent this thread from spinning too
      // fast. You should continue the business logic in a production
      // server rather than sleeping.
    }
    bthread_usleep(2 * 1000 * 1000);
  }
  return NULL;
}

int main(int argc, char* argv[]) {
  // Parse gflags. We recommend you to use gflags as well.
  GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);

  // A Channel represents a communication line to a Server. Notice that
  // Channel is thread-safe and can be shared by all threads in your program.
  brpc::Channel channel;

  // Initialize the channel, NULL means using default options.
  brpc::ChannelOptions options;
  options.protocol = brpc::PROTOCOL_MONGO;
  options.connection_type = FLAGS_connection_type;
  options.timeout_ms = FLAGS_timeout_ms /*milliseconds*/;
  options.connect_timeout_ms = FLAGS_connect_timeout_ms;
  options.max_retry = FLAGS_max_retry;
  LOG(INFO) << "passwd:" << FLAGS_password;
  // options.auth = new brpc::policy::MysqlAuthenticator(
  //     FLAGS_user, FLAGS_password, FLAGS_schema, FLAGS_params,
  //     FLAGS_collation);
  if (channel.Init(FLAGS_server.c_str(), FLAGS_port, &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel";
    return -1;
  }

  if (FLAGS_dummy_port >= 0) {
    brpc::StartDummyServerAt(FLAGS_dummy_port);
  }

  // test CRUD operations
  std::vector<bthread_t> bids;
  std::vector<pthread_t> pids;
  bids.resize(FLAGS_thread_num);
  pids.resize(FLAGS_thread_num);
  std::vector<SenderArgs> args;
  args.resize(FLAGS_thread_num);
  decltype(sender)* test_func = sender;
  if (FLAGS_op_type == 4) {
    test_func = delete_test;
  } else if (FLAGS_op_type = 2) {
    test_func = update_test;
  }
  for (int i = 0; i < FLAGS_thread_num; ++i) {
    args[i].base_index = i;
    args[i].mongo_channel = &channel;
    if (!FLAGS_use_bthread) {
      if (pthread_create(&pids[i], NULL, test_func, &args[i]) != 0) {
        LOG(ERROR) << "Fail to create pthread";
        return -1;
      }
    } else {
      if (bthread_start_background(&bids[i], NULL, test_func, &args[i]) != 0) {
        LOG(ERROR) << "Fail to create bthread";
        return -1;
      }
    }
  }

  while (!brpc::IsAskedToQuit()) {
    sleep(1);

    LOG(INFO) << "Accessing mysql-server at qps=" << g_latency_recorder.qps(1)
              << " latency=" << g_latency_recorder.latency(1);
  }

  LOG(INFO) << "mysql_client is going to quit";
  for (int i = 0; i < FLAGS_thread_num; ++i) {
    if (!FLAGS_use_bthread) {
      pthread_join(pids[i], NULL);
    } else {
      bthread_join(bids[i], NULL);
    }
  }

  return 0;
}
