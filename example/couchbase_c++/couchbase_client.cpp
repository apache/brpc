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

#include <brpc/couchbase.h>
#include <butil/logging.h>
#include <gflags/gflags.h>

#include <iostream>
#include <string>

// ANSI color codes for console output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

DEFINE_string(server, "localhost:11210", "IP Address of server");
int performOperations(brpc::CouchbaseOperations& couchbase_ops) {
  std::string add_key = "user::test_brpc_binprot";
  std::string add_value =
      R"({"name": "John Doe", "age": 30, "email": "john@example.com"})";

  brpc::CouchbaseOperations::Result add_result =
      couchbase_ops.add(add_key, add_value);
  if (add_result.success) {
    std::cout << GREEN << "ADD operation successful" << RESET << std::endl;
  } else {
    std::cout << RED << "ADD operation failed: " << add_result.error_message
              << RESET << std::endl;
  }

  // Try to ADD the same key again (should fail with key exists)
  brpc::CouchbaseOperations::Result add_result2 =
      couchbase_ops.add(add_key, add_value);
  if (add_result2.success) {
    std::cout << GREEN << "Second ADD operation unexpectedly successful"
              << RESET << std::endl;
  } else {
    std::cout << RED << "Second ADD operation failed as expected: "
              << add_result2.error_message << RESET << std::endl;
  }
  // Get operation using high-level method
  brpc::CouchbaseOperations::Result get_result = couchbase_ops.get(add_key);
  if (get_result.success) {
    std::cout << GREEN << "GET operation successful" << RESET << std::endl;
    std::cout << "GET value: " << get_result.value << std::endl;
  } else {
    std::cout << RED << "GET operation failed: " << get_result.error_message
              << RESET << std::endl;
  }

  // Add binprot item1 using high-level method
  std::string item1_key = "binprot_item1";
  brpc::CouchbaseOperations::Result item1_result =
      couchbase_ops.add(item1_key, add_value);
  if (item1_result.success) {
    std::cout << GREEN << "ADD binprot item1 successful" << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item1 failed: " << item1_result.error_message
              << RESET << std::endl;
  }

  // Add binprot item2 using high-level method
  std::string item2_key = "binprot_item2";
  brpc::CouchbaseOperations::Result item2_result =
      couchbase_ops.add(item2_key, add_value);
  if (item2_result.success) {
    std::cout << GREEN << "ADD binprot item2 successful" << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item2 failed: " << item2_result.error_message
              << RESET << std::endl;
  }

  // Add binprot item3 using high-level method
  std::string item3_key = "binprot_item3";
  brpc::CouchbaseOperations::Result item3_result =
      couchbase_ops.add(item3_key, add_value);
  if (item3_result.success) {
    std::cout << GREEN << "ADD binprot item3 successful" << RESET << std::endl;
  } else {
    std::cout << RED
              << "ADD binprot item3 failed: " << item3_result.error_message
              << RESET << std::endl;
  }

  // Perform an UPSERT on the existing key using high-level method
  std::string upsert_key = "user::test_brpc_binprot";
  std::string upsert_value =
      R"({"name": "Upserted Jane Doe", "age": 28, "email": "upserted.doe@example.com"})";
  brpc::CouchbaseOperations::Result upsert_result =
      couchbase_ops.upsert(upsert_key, upsert_value);
  if (upsert_result.success) {
    std::cout
        << GREEN
        << "UPSERT operation successful when the document exists in the server"
        << RESET << std::endl;
  } else {
    std::cout
        << RED
        << "UPSERT operation failed when the document exists in the server: "
        << upsert_result.error_message << RESET << std::endl;
  }
  // Do UPSERT operation on a new document using high-level method
  std::string new_upsert_key = "user::test_brpc_new_upsert";
  std::string new_upsert_value =
      R"({"name": "Jane Doe", "age": 28, "email": "jane.doe@example.com"})";
  brpc::CouchbaseOperations::Result new_upsert_result =
      couchbase_ops.upsert(new_upsert_key, new_upsert_value);
  if (new_upsert_result.success) {
    std::cout << GREEN
              << "UPSERT operation successful when the document doesn't exist "
                 "in the server"
              << RESET << std::endl;
  } else {
    std::cout << RED
              << "UPSERT operation failed when document does not exist in the "
                 "server: "
              << new_upsert_result.error_message << RESET << std::endl;
  }

  // Check the upserted data using high-level method
  std::string check_key = "user::test_brpc_new_upsert";
  brpc::CouchbaseOperations::Result check_result = couchbase_ops.get(check_key);
  if (check_result.success) {
    std::cout << GREEN << "GET after UPSERT operation successful - Value: "
              << check_result.value << RESET << std::endl;
  } else {
    std::cout << RED << "GET after UPSERT operation failed: "
              << check_result.error_message << RESET << std::endl;
  }

  // Delete a non-existent key using high-level method
  std::string delete_key = "Nonexistent_key";
  brpc::CouchbaseOperations::Result delete_result =
      couchbase_ops.delete_(delete_key);
  if (delete_result.success) {
    std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
  } else {
    std::cout << RED << "DELETE operation failed: as expected "
              << delete_result.error_message << RESET << std::endl;
  }

  // Delete the existing key using high-level method
  std::string delete_existing_key = "user::test_brpc_binprot";
  brpc::CouchbaseOperations::Result delete_existing_result =
      couchbase_ops.delete_(delete_existing_key);
  if (delete_existing_result.success) {
    std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
  } else {
    std::cout << RED << "DELETE operation failed: "
              << delete_existing_result.error_message << RESET << std::endl;
  }

  // Retrieve Collection ID for scope `_default` and collection
  // `col1`
  const std::string scope_name = "_default";  // default scope
  std::string collection_name = "col1";       // target collection
  // ------------------------------------------------------------------
  // Collection-scoped CRUD operations (only if collection id was retrieved)
  // ------------------------------------------------------------------
  // 1. ADD in collection using high-level method
  std::string coll_key = "user::collection_doc";
  std::string coll_value = R"({"type":"collection","op":"add","v":1})";
  brpc::CouchbaseOperations::Result coll_add_result =
      couchbase_ops.add(coll_key, coll_value, collection_name);
  if (coll_add_result.success) {
    std::cout << GREEN << "Collection ADD success" << RESET << std::endl;
  } else {
    std::cout << RED
              << "Collection ADD failed: " << coll_add_result.error_message
              << RESET << std::endl;
  }
  // 2. GET from collection using high-level method
  brpc::CouchbaseOperations::Result coll_get_result =
      couchbase_ops.get(coll_key, collection_name);
  if (coll_get_result.success) {
    std::cout << GREEN
              << "Collection GET success value=" << coll_get_result.value
              << RESET << std::endl;
  } else {
    std::cout << RED
              << "Collection GET failed: " << coll_get_result.error_message
              << RESET << std::endl;
  }

  // 3. UPSERT in collection using high-level method
  std::string coll_upsert_value =
      R"({"type":"collection","op":"upsert","v":2})";
  brpc::CouchbaseOperations::Result coll_upsert_result =
      couchbase_ops.upsert(coll_key, coll_upsert_value, collection_name);
  if (coll_upsert_result.success) {
    std::cout << GREEN << "Collection UPSERT success" << RESET << std::endl;
  } else {
    std::cout << RED << "Collection UPSERT failed: "
              << coll_upsert_result.error_message << RESET << std::endl;
  }

  // 4. GET again to verify upsert using high-level method
  brpc::CouchbaseOperations::Result coll_get2_result =
      couchbase_ops.get(coll_key, collection_name);
  if (coll_get2_result.success) {
    std::cout << GREEN
              << "Collection GET(after upsert) value=" << coll_get2_result.value
              << RESET << std::endl;
  }

  // 5. DELETE from collection using high-level method
  brpc::CouchbaseOperations::Result coll_del_result =
      couchbase_ops.delete_(coll_key, collection_name);
  if (coll_del_result.success) {
    std::cout << GREEN << "Collection DELETE success" << RESET << std::endl;
  } else {
    std::cout << RED
              << "Collection DELETE failed: " << coll_del_result.error_message
              << RESET << std::endl;
  }

  // ------------------------------------------------------------------
  // Pipeline Operations Demo
  // ------------------------------------------------------------------
  std::cout << GREEN << "\n=== Pipeline Operations Demo ===" << RESET
            << std::endl;

  // Begin a new pipeline
  if (!couchbase_ops.beginPipeline()) {
    std::cout << RED << "Failed to begin pipeline" << RESET << std::endl;
    return -1;
  }

  std::cout << "Pipeline started. Adding multiple operations..." << std::endl;

  // Add multiple operations to the pipeline
  std::string pipeline_key1 = "pipeline::doc1";
  std::string pipeline_key2 = "pipeline::doc2";
  std::string pipeline_key3 = "pipeline::doc3";
  std::string pipeline_value1 = R"({"operation": "pipeline_add", "id": 1})";
  std::string pipeline_value2 = R"({"operation": "pipeline_upsert", "id": 2})";
  std::string pipeline_value3 = R"({"operation": "pipeline_add", "id": 3})";

  // Pipeline operations - all prepared but not yet executed
  bool pipeline_success = true;
  pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::ADD, pipeline_key1, pipeline_value1);
  pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::UPSERT, pipeline_key2, pipeline_value2);
  pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::ADD, pipeline_key3, pipeline_value3);
  pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::GET, pipeline_key1);
  pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::GET, pipeline_key2);

  if (!pipeline_success) {
    std::cout << RED << "Failed to add operations to pipeline" << RESET
              << std::endl;
    couchbase_ops.clearPipeline();
    return -1;
  }

  std::cout << "Added " << couchbase_ops.getPipelineSize()
            << " operations to pipeline" << std::endl;

  // Execute all operations in a single network call
  std::cout << "Executing pipeline operations..." << std::endl;
  std::vector<brpc::CouchbaseOperations::Result> pipeline_results =
      couchbase_ops.executePipeline();

  // Process results in order
  std::cout << GREEN << "Pipeline execution completed. Results:" << RESET
            << std::endl;
  for (size_t i = 0; i < pipeline_results.size(); ++i) {
    const auto& result = pipeline_results[i];
    if (result.success) {
      if (!result.value.empty()) {
        std::cout << GREEN << "  Operation " << (i + 1)
                  << " SUCCESS - Value: " << result.value << RESET << std::endl;
      } else {
        std::cout << GREEN << "  Operation " << (i + 1) << " SUCCESS" << RESET
                  << std::endl;
      }
    } else {
      std::cout << RED << "  Operation " << (i + 1)
                << " FAILED: " << result.error_message << RESET << std::endl;
    }
  }

  // Demonstrate pipeline with collection operations
  std::cout << GREEN << "\n=== Pipeline with Collection Operations ===" << RESET
            << std::endl;

  if (!couchbase_ops.beginPipeline()) {
    std::cout << RED << "Failed to begin collection pipeline" << RESET
              << std::endl;
    return -1;
  }

  std::string coll_pipeline_key1 = "coll_pipeline::doc1";
  std::string coll_pipeline_key2 = "coll_pipeline::doc2";
  std::string coll_pipeline_value1 =
      R"({"collection_operation": "pipeline_add", "id": 1})";
  std::string coll_pipeline_value2 =
      R"({"collection_operation": "pipeline_upsert", "id": 2})";

  // Add collection-scoped operations to pipeline
  bool coll_pipeline_success = true;
  coll_pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::ADD, coll_pipeline_key1, coll_pipeline_value1,
      collection_name);
  coll_pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::UPSERT, coll_pipeline_key2,
      coll_pipeline_value2, collection_name);
  coll_pipeline_success &= couchbase_ops.pipelineRequest(
      brpc::CouchbaseOperations::GET, coll_pipeline_key1, "", collection_name);
  coll_pipeline_success &=
      couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE,
                                    coll_pipeline_key1, "", collection_name);

  if (!coll_pipeline_success) {
    std::cout << RED << "Failed to add collection operations to pipeline"
              << RESET << std::endl;
    couchbase_ops.clearPipeline();
    return -1;
  }

  // Execute collection pipeline
  std::vector<brpc::CouchbaseOperations::Result> coll_pipeline_results =
      couchbase_ops.executePipeline();

  std::cout << GREEN
            << "Collection pipeline execution completed. Results:" << RESET
            << std::endl;
  for (size_t i = 0; i < coll_pipeline_results.size(); ++i) {
    const auto& result = coll_pipeline_results[i];
    if (result.success) {
      if (!result.value.empty()) {
        std::cout << GREEN << "  Collection Operation " << (i + 1)
                  << " SUCCESS - Value: " << result.value << RESET << std::endl;
      } else {
        std::cout << GREEN << "  Collection Operation " << (i + 1) << " SUCCESS"
                  << RESET << std::endl;
      }
    } else {
      std::cout << RED << "  Collection Operation " << (i + 1)
                << " FAILED: " << result.error_message << RESET << std::endl;
    }
  }

  // Clean up remaining pipeline documents
  std::cout << GREEN << "\n=== Cleanup Pipeline Demo ===" << RESET << std::endl;
  if (couchbase_ops.beginPipeline()) {
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE,
                                  pipeline_key1);
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE,
                                  pipeline_key2);
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE,
                                  pipeline_key3);
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE,
                                  coll_pipeline_key2, "", collection_name);

    std::vector<brpc::CouchbaseOperations::Result> cleanup_results =
        couchbase_ops.executePipeline();
    std::cout << "Cleanup completed (" << cleanup_results.size()
              << " operations)" << std::endl;
  }

  std::cout << GREEN
            << "\n=== All operations completed successfully! ===" << RESET
            << std::endl;
}
int main() {
  // Create CouchbaseOperations instance for high-level operations
  brpc::CouchbaseOperations couchbase_ops;

  // std::cout << GREEN << "Using high-level CouchbaseOperations interface"
  //           << RESET << std::endl;

  // Ask username and password for authentication
  std::string username = "Administrator";
  std::string password = "password";
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

  // Use high-level authentication method
  // when connecting to capella use couchbase_ops.authenticate(username,
  // password, FLAGS_server, true, "path/to/cert.txt");
  brpc::CouchbaseOperations::Result auth_result =
      couchbase_ops.authenticate(username, password, FLAGS_server, "testing");
  if (!auth_result.success) {
    LOG(ERROR) << "Authentication failed: " << auth_result.error_message;
    return -1;
  }

  std::cout
      << GREEN
      << "Authentication successful, proceeding with Couchbase operations..."
      << RESET << std::endl;

  performOperations(couchbase_ops);

  // Change bucket Selection
  std::string bucket_name = "testing";
  while (bucket_name.empty()) {
    std::cout << "Enter Couchbase bucket name: ";
    std::cin >> bucket_name;
    if (bucket_name.empty()) {
      std::cout << "Bucket name cannot be empty. Please enter again."
                << std::endl;
      continue;
    }
  }

  // Use high-level bucket selection method
  brpc::CouchbaseOperations::Result bucket_result =
      couchbase_ops.selectBucket(bucket_name);
  if (!bucket_result.success) {
    LOG(ERROR) << "Bucket selection failed: " << bucket_result.error_message;
    return -1;
  } else {
    std::cout << GREEN << "Bucket Selection Successful" << RESET << std::endl;
  }
  // Add operation using high-level method
  performOperations(couchbase_ops);
  return 0;
}
