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

#include <brpc/channel.h>
#include <brpc/couchbase.h>
#include <brpc/policy/couchbase_authenticator.h>
#include <brpc/policy/couchbase_protocol.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <gflags/gflags.h>

#include <iomanip>
#include <mutex>

// ANSI color codes for console output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

DEFINE_string(server, "127.0.0.1:11210", "IP Address of server");
DEFINE_string(connection_type, "single",
              "Connection type. Available values: single, pooled, short");
// DEFINE_string(username, "Administrator", "Couchbase username");
// DEFINE_string(password, "password", "Couchbase password");
// DEFINE_string(bucket_name, "testing", "Couchbase bucket name");
DEFINE_bool(use_bthread, true, "Use bthread to send requests");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)");
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(exptime, 0,
             "The to-be-got data will be expired after so many seconds");

uint32_t batch_size = 10;

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");
butil::static_atomic<int> g_sender_count = BUTIL_STATIC_ATOMIC_INIT(0);

// Global variables for timing statistics
std::vector<double> thread_response_times;
std::mutex timing_mutex;

int main() {
  brpc::Channel channel;

  // Initialize the channel, NULL means using default options.
  brpc::ChannelOptions options;
  options.protocol = brpc::PROTOCOL_COUCHBASE;
  options.connection_type = FLAGS_connection_type;
  options.timeout_ms = 2000 /*milliseconds*/;
  options.max_retry = 2;

  if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(),
                   &options) != 0) {
    LOG(ERROR) << "Fail to initialize channel";
    return -1;
  }
  std::cout << GREEN << "Channel initialized successfully" << RESET
            << std::endl;
  // Couchbase Authentication packet(SASL Auth) is now present in the channel
  // now a request can be sent with which auth packet will also be sent.

  // create authrequest and authresponse
  brpc::Controller cntl;
  brpc::CouchbaseRequest auth_request;
  brpc::CouchbaseResponse auth_response;

  // Ask username and password for authentication
  std::string username;
  std::string password;
  while (username.empty() || password.empty()) {
    std::cout << "Enter Couchbase username: ";
    std::cin >> username;
    if (username.empty()) {
      std::cout << "Username cannot be empty. Please enter again." << std::endl;
      continue;
    }
    std::cout << "Enter Couchbase password: ";
    std::cin >> password;
    if (password.empty()) {
      std::cout << "Password cannot be empty. Please enter again." << std::endl;
      continue;
    }
  }

  if (!auth_request.Authenticate(username.c_str(), password.c_str())) {
    LOG(ERROR) << "Fail to create authentication request";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &auth_request, &auth_response, NULL);

  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  cntl.Reset();

  std::cout
      << GREEN
      << "Authentication successful, proceeding with Couchbase operations..."
      << RESET << std::endl;

  // Select bucket
  std::string bucket_name;
  while (bucket_name.empty()) {
    std::cout << "Enter Couchbase bucket name: ";
    std::cin >> bucket_name;
    if (bucket_name.empty()) {
      std::cout << "Bucket name cannot be empty. Please enter again."
                << std::endl;
      continue;
    }
  }
  brpc::CouchbaseRequest select_bucket_request;
  brpc::CouchbaseResponse select_bucket_response;
  if (!select_bucket_request.SelectBucket(bucket_name.c_str())) {
    LOG(ERROR) << "Fail to SELECT bucket request";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &select_bucket_request,
                     &select_bucket_response, NULL);

  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  } else {
    // Check ADD operation response status
    uint64_t cas_value;
    if (select_bucket_response.PopSelectBucket(&cas_value)) {
      std::cout << GREEN << "Bucket Selection Successful, CAS: " << cas_value
                << RESET << std::endl;
    } else {
      std::cout << RED << select_bucket_response.LastError() << RESET
                << std::endl;
    }
  }

  cntl.Reset();

  // Add operation
  brpc::CouchbaseRequest add_request;
  brpc::CouchbaseResponse add_response;
  if (!add_request.Add(
          butil::string_printf("user::test_brpc_binprot"),
          R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
          0xdeadbeef, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to ADD request";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);

  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  } else {
    // Check ADD operation response status
    uint64_t cas_value;
    if (add_response.PopAdd(&cas_value)) {
      std::cout << GREEN << "ADD operation successful, CAS: " << cas_value
                << RESET << std::endl;
    } else {
      std::cout << RED << add_response.LastError() << RESET << std::endl;
    }
  }

  cntl.Reset();

  if (!add_request.Add(
          butil::string_printf("user::test_brpc_binprot"),
          R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
          0xdeadbeef, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to ADD request";
    return -1;
  }
  channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);

  // Check second ADD operation response status (should fail with key exists)
  if (cntl.Failed()) {
    LOG(ERROR) << "RPC call failed: " << cntl.ErrorText();
  } else {
    uint64_t cas_value;
    if (add_response.PopAdd(&cas_value)) {
      std::cout << GREEN
                << "Second ADD operation unexpectedly successful, CAS: "
                << cas_value << RESET << std::endl;
    } else {
      std::cout << RED << "Second ADD operation failed as expected: "
                << add_response.LastError() << RESET << std::endl;
    }
  }

  cntl.Reset();
  // Get operation
  brpc::CouchbaseRequest get_request;
  brpc::CouchbaseResponse get_response;
  if (!get_request.Get(butil::string_printf("%s", "user::test_brpc_binprot"))) {
    LOG(ERROR) << "Fail to GET request";
    return -1;
  }
  channel.CallMethod(NULL, &cntl, &get_request, &get_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check GET operation response status
  std::string value;
  uint32_t flags = 0;
  uint64_t cas = 0;
  if (get_response.PopGet(&value, &flags, &cas)) {
    std::cout << GREEN << "GET operation successful" << RESET << std::endl;
    std::cout << "GET value: " << value << std::endl;
    std::cout << "Flags: " << flags << std::endl;
    std::cout << "CAS: " << cas << std::endl;
  } else {
    std::cout << RED << "GET operation failed: " << get_response.LastError()
              << RESET << std::endl;
    std::cout << "Raw response (hex): ";
    for (char c : get_response.raw_buffer().to_string()) {
      printf("%02x ", static_cast<unsigned char>(c));
    }
    std::cout << std::endl;
  }
  cntl.Reset();

  // Create a new request for binprot item1
  brpc::CouchbaseRequest add_request1;
  brpc::CouchbaseResponse add_response1;
  if (!add_request1.Add(
          butil::string_printf("binprot_item1"),
          R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
          0xdeadbeef, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to ADD request";
    return -1;
  }
  std::cout << "Sending ADD request for binprot_item1, pipelined count: "
            << add_request1.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &add_request1, &add_response1, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check ADD binprot item1 response status
  uint64_t cas_value;
  if (add_response1.PopAdd(&cas_value)) {
    std::cout << GREEN << "ADD binprot item1 successful, CAS: " << cas_value
              << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item1 failed: " << add_response1.LastError()
              << RESET << std::endl;
  }
  cntl.Reset();

  // Create a new request for binprot item2
  brpc::CouchbaseRequest add_request2;
  brpc::CouchbaseResponse add_response2;
  if (!add_request2.Add(
          butil::string_printf("binprot_item2"),
          R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
          0xdeadbeef, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to ADD request";
    return -1;
  }
  std::cout << "Sending ADD request for binprot_item2, pipelined count: "
            << add_request2.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &add_request2, &add_response2, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check ADD binprot item2 response status
  if (add_response2.PopAdd(&cas_value)) {
    std::cout << GREEN << "ADD binprot item2 successful, CAS: " << cas_value
              << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item2 failed: " << add_response2.LastError()
              << RESET << std::endl;
  }
  cntl.Reset();

  // Create a new request for binprot item3
  brpc::CouchbaseRequest add_request3;
  brpc::CouchbaseResponse add_response3;
  if (!add_request3.Add(
          butil::string_printf("binprot_item3"),
          R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
          0xdeadbeef, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to ADD request";
    return -1;
  }
  std::cout << "Sending ADD request for binprot_item3, pipelined count: "
            << add_request3.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &add_request3, &add_response3, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check ADD binprot item3 response status
  if (add_response3.PopAdd(&cas_value)) {
    std::cout << GREEN << "ADD binprot item3 successful, CAS: " << cas_value
              << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item3 failed: " << add_response3.LastError()
              << RESET << std::endl;
  }
  cntl.Reset();

  // pipeline ADD operation
  brpc::CouchbaseRequest pipeline_request;
  brpc::CouchbaseResponse pipeline_response;
  for (int i = 0; i < 10; ++i) {
    if (!pipeline_request.Add(
            butil::string_printf("pipeline_item_%d", i),
            R"({"name": "Pipeline User", "age": 25, "email": "pipeline@example.com"})",
            0xdeadbeef, FLAGS_exptime, 0)) {
      LOG(ERROR) << "Fail to ADD request";
      return -1;
    }
  }
  std::cout << "Sending pipeline ADD request, pipelined count: "
            << pipeline_request.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &pipeline_request, &pipeline_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check each pipelined operation response status
  std::cout << "Processing pipeline responses..." << std::endl;
  int successful_operations = 0;
  int failed_operations = 0;

  for (int i = 0; i < 10; ++i) {
    uint64_t cas_value;
    if (pipeline_response.PopAdd(&cas_value)) {
      std::cout << GREEN << "Pipeline ADD operation " << i
                << " successful, CAS: " << cas_value << RESET << std::endl;
      successful_operations++;
    } else {
      std::cout << RED << "Pipeline ADD operation " << i
                << " failed: " << pipeline_response.LastError() << RESET
                << std::endl;
      failed_operations++;
    }
  }

  std::cout << "Pipeline summary: " << successful_operations << " successful, "
            << failed_operations << " failed operations" << std::endl;

  cntl.Reset();

  // Example of mixed pipeline operations (GET operations on existing and
  // non-existing keys)
  std::cout << "\n=== Testing Mixed Pipeline Operations ===" << std::endl;
  brpc::CouchbaseRequest mixed_pipeline_request;
  brpc::CouchbaseResponse mixed_pipeline_response;

  // Add some GET operations to the pipeline - some will succeed, some will fail
  std::vector<std::string> keys_to_get = {
      "user::test_brpc_binprot",  // Should exist
      "binprot_item1",            // Should exist
      "nonexistent_key1",         // Should fail
      "binprot_item2",            // Should exist
      "nonexistent_key2",         // Should fail
      "binprot_item3"             // Should exist
  };

  for (const auto& key : keys_to_get) {
    if (!mixed_pipeline_request.Get(key)) {
      LOG(ERROR) << "Fail to add GET request for key: " << key;
      return -1;
    }
  }

  std::cout << "Sending mixed pipeline GET request, pipelined count: "
            << mixed_pipeline_request.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &mixed_pipeline_request,
                     &mixed_pipeline_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Process each GET response
  int successful_gets = 0;
  int failed_gets = 0;

  for (size_t i = 0; i < keys_to_get.size(); ++i) {
    std::string value;
    uint32_t flags;
    uint64_t cas;

    if (mixed_pipeline_response.PopGet(&value, &flags, &cas)) {
      std::cout << GREEN << "GET '" << keys_to_get[i]
                << "' successful - Value length: " << value.length()
                << ", Flags: " << flags << ", CAS: " << cas << RESET
                << std::endl;
      successful_gets++;
    } else {
      std::cout << RED << "GET '" << keys_to_get[i]
                << "' failed: " << mixed_pipeline_response.LastError() << RESET
                << std::endl;
      failed_gets++;
    }
  }

  std::cout << "Mixed pipeline summary: " << successful_gets << " successful, "
            << failed_gets << " failed GET operations" << std::endl;

  cntl.Reset();

  // perform an upsert on the existing key
  brpc::CouchbaseRequest upsert_request;
  brpc::CouchbaseResponse upsert_response;
  if (!upsert_request.Upsert(
          butil::string_printf("user::test_brpc_binprot"),
          R"({"name": "Upserted Jane Doe", "age": 28, "email": "upserted.doe@example.com"})",
          0, FLAGS_exptime, 0)) {
    LOG(ERROR) << "Fail to add UPSERT request for key: user::test_brpc_binprot";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &upsert_request, &upsert_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }
  // Check UPSERT operation response status
  if (upsert_response.PopUpsert(&cas_value)) {
    std::cout << GREEN
              << "UPSERT operation successful when the document exists in the "
                 "server, CAS: "
              << cas_value << RESET << std::endl;
  } else {
    std::cout << RED
              << "UPSERT operation failed, when the document does exists in "
                 "the server: "
              << upsert_response.LastError() << RESET << std::endl;
  }

  cntl.Reset();
  // do the upsert operation
  brpc::CouchbaseRequest upsert_request_new_doc;
  brpc::CouchbaseResponse upsert_response_new_doc;
  if (!upsert_request_new_doc.Upsert(
          butil::string_printf("user::test_brpc_new_upsert"),
          R"({"name": "Jane Doe", "age": 28, "email": "jane.doe@example.com"})",
          0, FLAGS_exptime, 0)) {
    LOG(ERROR)
        << "Fail to add UPSERT request for key: user::test_brpc_new_upsert";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &upsert_request_new_doc,
                     &upsert_response_new_doc, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }
  // Check UPSERT operation response status
  if (upsert_response_new_doc.PopUpsert(&cas_value)) {
    std::cout << GREEN
              << "UPSERT operation successful when the document doesn't exists "
                 "in the server, CAS: "
              << cas_value << RESET << std::endl;
  } else {
    std::cout << RED
              << "UPSERT operation failed when document does not exists in the "
                 "server: "
              << upsert_response_new_doc.LastError() << RESET << std::endl;
  }

  cntl.Reset();

  // check the upserted data
  brpc::CouchbaseRequest check_upsert_request;
  brpc::CouchbaseResponse check_upsert_response;
  if (!check_upsert_request.Get(
          butil::string_printf("user::test_brpc_new_upsert"))) {
    LOG(ERROR) << "Fail to GET request for key: user::test_brpc_new_upsert";
    return -1;
  }

  std::cout
      << "Sending GET request for user::test_brpc_new_upsert, pipelined count: "
      << check_upsert_request.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &check_upsert_request, &check_upsert_response,
                     NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check GET operation response status
  if (check_upsert_response.PopGet(&value, &flags, &cas)) {
    std::cout << GREEN
              << "GET after UPSERT operation successful - Value: " << value
              << ", Flags: " << flags << ", CAS: " << cas << RESET << std::endl;
  } else {
    std::cout << RED << "GET after UPSERT operation failed: "
              << check_upsert_response.LastError() << RESET << std::endl;
  }

  cntl.Reset();

  // delete the Non-existent Key
  brpc::CouchbaseRequest delete_request;
  brpc::CouchbaseResponse delete_response;
  if (!delete_request.Delete(butil::string_printf("Nonexistent_key"))) {
    LOG(ERROR) << "Fail to DELETE request for key: Nonexistent_key";
    return -1;
  }

  std::cout << "Sending DELETE request for Nonexistent_key, pipelined count: "
            << delete_request.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &delete_request, &delete_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check DELETE operation response status
  if (delete_response.PopDelete()) {
    std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
  } else {
    std::cout << RED << "DELETE operation failed: as expected"
              << delete_response.LastError() << RESET << std::endl;
  }

  cntl.Reset();

  // delete the existing key
  brpc::CouchbaseRequest delete_existing_request;
  brpc::CouchbaseResponse delete_existing_response;
  if (!delete_existing_request.Delete(
          butil::string_printf("user::test_brpc_binprot"))) {
    LOG(ERROR) << "Fail to DELETE request for key: user::test_brpc_binprot";
    return -1;
  }

  std::cout
      << "Sending DELETE request for user::test_brpc_binprot, pipelined count: "
      << delete_existing_request.pipelined_count() << std::endl;
  channel.CallMethod(NULL, &cntl, &delete_existing_request,
                     &delete_existing_response, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    return -1;
  }

  // Check DELETE operation response status
  if (delete_existing_response.PopDelete()) {
    std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
  } else {
    std::cout << RED << "DELETE operation failed: "
              << delete_existing_response.LastError() << RESET << std::endl;
  }

  cntl.Reset();

  // Retrieve Collection ID for scope `_default` and collection
  // `testing_collection`

  brpc::CouchbaseRequest get_collection_request;
  brpc::CouchbaseResponse get_collection_response;
  uint8_t testing_collection_id = 0;  // will hold the numeric collection id
  const std::string scope_name = "_default";           // default scope
  std::string collection_name = "testing_collection";  // target collection
  // enter collection name as user input
  std::cout << "Enter collection name (default 'testing_collection'): ";
  std::string user_input;
  std::cin >> user_input;
  if (!user_input.empty()) {
    collection_name = user_input;
  }

  if (!get_collection_request.GetCollectionId(scope_name.c_str(),
                                              collection_name.c_str())) {
    LOG(ERROR) << "Fail to build GetCollectionId request for scope '"
               << scope_name << "' collection '" << collection_name << "'";
    return -1;
  }

  channel.CallMethod(NULL, &cntl, &get_collection_request,
                     &get_collection_response, NULL);

  if (cntl.Failed()) {
    LOG(ERROR) << "Fail to access Couchbase for GetCollectionId, "
               << cntl.ErrorText();
    return -1;
  }

  if (get_collection_response.PopCollectionId(&testing_collection_id)) {
    std::cout << GREEN << "Retrieved collection id for _default."
              << collection_name << " = "
              << static_cast<unsigned int>(testing_collection_id)
              << " dec=" << static_cast<unsigned int>(testing_collection_id)
              << ", hex=0x" << std::hex
              << static_cast<unsigned int>(testing_collection_id) << RESET
              << std::endl;
  } else {
    std::cout << RED << "Failed to retrieve collection id for _default."
              << collection_name << ": " << get_collection_response.LastError()
              << RESET << std::endl;
    // We continue, but subsequent collection operations will skip if id=0
  }
  cntl.Reset();
  // ------------------------------------------------------------------
  // Collection-scoped CRUD operations (only if collection id was retrieved)
  // ------------------------------------------------------------------
  if (testing_collection_id != 0) {
    // 1. ADD in collection
    brpc::CouchbaseRequest coll_add_req;
    brpc::CouchbaseResponse coll_add_resp;
    const std::string coll_key = "user::collection_doc";
    if (!coll_add_req.Add(
            coll_key.c_str(), R"({"type":"collection","op":"add","v":1})",
            0xabcddcba, FLAGS_exptime, 0, (uint8_t)testing_collection_id)) {
      LOG(ERROR) << "Fail to build collection ADD request";
    } else {
      channel.CallMethod(NULL, &cntl, &coll_add_req, &coll_add_resp, NULL);
      if (cntl.Failed()) {
        LOG(ERROR) << "Collection ADD RPC failed: " << cntl.ErrorText();
      } else {
        uint64_t ccas;
        if (coll_add_resp.PopAdd(&ccas)) {
          std::cout << GREEN << "Collection ADD success, CAS=" << ccas << RESET
                    << std::endl;
        } else {
          std::cout << RED
                    << "Collection ADD failed: " << coll_add_resp.LastError()
                    << RESET << std::endl;
        }
      }
      cntl.Reset();
    }

    // 2. GET from collection
    brpc::CouchbaseRequest coll_get_req;
    brpc::CouchbaseResponse coll_get_resp;
    if (!coll_get_req.Get(coll_key.c_str(), (uint8_t)testing_collection_id)) {
      LOG(ERROR) << "Fail to build collection GET request";
    } else {
      channel.CallMethod(NULL, &cntl, &coll_get_req, &coll_get_resp, NULL);
      if (cntl.Failed()) {
        LOG(ERROR) << "Collection GET RPC failed: " << cntl.ErrorText();
      } else {
        std::string v;
        uint32_t f = 0;
        uint64_t ccas = 0;
        if (coll_get_resp.PopGet(&v, &f, &ccas)) {
          std::cout << GREEN << "Collection GET success value=" << v
                    << ", CAS=" << ccas << RESET << std::endl;
        } else {
          std::cout << RED
                    << "Collection GET failed: " << coll_get_resp.LastError()
                    << RESET << std::endl;
        }
      }
      cntl.Reset();
    }

    // 3. UPSERT in collection
    brpc::CouchbaseRequest coll_upsert_req;
    brpc::CouchbaseResponse coll_upsert_resp;
    if (!coll_upsert_req.Upsert(
            coll_key.c_str(), R"({"type":"collection","op":"upsert","v":2})", 0,
            FLAGS_exptime, 0, (uint8_t)testing_collection_id)) {
      LOG(ERROR) << "Fail to build collection UPSERT request";
    } else {
      channel.CallMethod(NULL, &cntl, &coll_upsert_req, &coll_upsert_resp,
                         NULL);
      if (cntl.Failed()) {
        LOG(ERROR) << "Collection UPSERT RPC failed: " << cntl.ErrorText();
      } else {
        uint64_t ccas;
        if (coll_upsert_resp.PopUpsert(&ccas)) {
          std::cout << GREEN << "Collection UPSERT success, CAS=" << ccas
                    << RESET << std::endl;
        } else {
          std::cout << RED << "Collection UPSERT failed: "
                    << coll_upsert_resp.LastError() << RESET << std::endl;
        }
      }
      cntl.Reset();
    }

    // 4. GET again to verify upsert
    brpc::CouchbaseRequest coll_get2_req;
    brpc::CouchbaseResponse coll_get2_resp;
    if (coll_get2_req.Get(coll_key.c_str(), (uint8_t)testing_collection_id)) {
      channel.CallMethod(NULL, &cntl, &coll_get2_req, &coll_get2_resp, NULL);
      if (!cntl.Failed()) {
        std::string v;
        uint32_t f = 0;
        uint64_t ccas = 0;
        if (coll_get2_resp.PopGet(&v, &f, &ccas)) {
          std::cout << GREEN << "Collection GET(after upsert) value=" << v
                    << RESET << std::endl;
        }
      }
      cntl.Reset();
    }

    // 5. DELETE from collection
    brpc::CouchbaseRequest coll_del_req;
    brpc::CouchbaseResponse coll_del_resp;
    if (!coll_del_req.Delete(coll_key.c_str(),
                             (uint8_t)testing_collection_id)) {
      LOG(ERROR) << "Fail to build collection DELETE request";
    } else {
      channel.CallMethod(NULL, &cntl, &coll_del_req, &coll_del_resp, NULL);
      if (cntl.Failed()) {
        LOG(ERROR) << "Collection DELETE RPC failed: " << cntl.ErrorText();
      } else {
        if (coll_del_resp.PopDelete()) {
          std::cout << GREEN << "Collection DELETE success" << RESET
                    << std::endl;
        } else {
          std::cout << RED
                    << "Collection DELETE failed: " << coll_del_resp.LastError()
                    << RESET << std::endl;
        }
      }
      cntl.Reset();
    }
  } else {
    std::cout << RED
              << "Skipping collection-scoped CRUD operations (collection id "
                 "not available)"
              << RESET << std::endl;
  }
  return 0;
}
