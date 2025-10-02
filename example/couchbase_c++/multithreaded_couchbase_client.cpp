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
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <butil/time.h>
#include <gflags/gflags.h>

#include <iostream>
#include <string>
#include <vector>

// ANSI color codes
#define GREEN "\033[32m"
#define RED "\033[31m"
#define BLUE "\033[34m"
#define YELLOW "\033[33m"
#define CYAN "\033[36m"
#define RESET "\033[0m"

DEFINE_string(server, "127.0.0.1:11210", "IP Address of server");
DEFINE_string(connection_type, "single", "Connection type");
DEFINE_int32(operations_per_thread, 50,
             "Number of operations each thread should perform");
DEFINE_int32(sleep_ms, 100, "Sleep time between operations in milliseconds");

const int NUM_THREADS = 16;
const int THREADS_PER_BUCKET = 4;

// Simple global config
struct {
  std::string username;
  std::string password;
  std::vector<std::string> bucket_names;
  std::vector<std::string> collection_names;
} g_config;

// Thread arguments
struct ThreadArgs {
  int thread_id;
  int bucket_id;
  std::string bucket_name;
};

// Simple operation function using high-level API
bool perform_operation(brpc::CouchbaseOperations& couchbase_ops,
                       const std::string& key,
                       const std::string& collection = "_default") {
  // Simple ADD operation
  std::string value = butil::string_printf(
      R"({"thread_id": %llu, "timestamp": %lld})",
      (unsigned long long)bthread_self(), butil::gettimeofday_us());

  brpc::CouchbaseOperations::Result result =
      couchbase_ops.Add(key, value, collection);
  return result.success;
}

// Thread worker function using high-level API
void* thread_worker(void* arg) {
  ThreadArgs* args = static_cast<ThreadArgs*>(arg);

  LOG(INFO) << "Thread " << args->thread_id << " starting on bucket "
            << args->bucket_name;

  // Create CouchbaseOperations instance for this thread
  brpc::CouchbaseOperations couchbase_ops;

  // Authentication using high-level method
  brpc::CouchbaseOperations::Result auth_result = couchbase_ops.Authenticate(
      g_config.username, g_config.password, FLAGS_server, false, "");
  if (!auth_result.success) {
    LOG(ERROR) << "Thread " << args->thread_id << ": Auth failed - "
               << auth_result.error_message;
    return NULL;
  }

  // Select bucket using high-level method
  brpc::CouchbaseOperations::Result bucket_result =
      couchbase_ops.SelectBucket(args->bucket_name);
  if (!bucket_result.success) {
    LOG(ERROR) << "Thread " << args->thread_id << ": Bucket selection failed - "
               << bucket_result.error_message;
    return NULL;
  }

  LOG(INFO) << "Thread " << args->thread_id << " connected to bucket "
            << args->bucket_name;

  // Perform operations
  int success_count = 0;
  for (int i = 0; i < FLAGS_operations_per_thread; ++i) {
    std::string key =
        butil::string_printf("thread_%d_op_%d", args->thread_id, i);

    // Choose collection if available, otherwise use default
    std::string collection = "_default";
    if (!g_config.collection_names.empty()) {
      collection =
          g_config.collection_names[i % g_config.collection_names.size()];
    }

    if (perform_operation(couchbase_ops, key, collection)) {
      success_count++;
    }

    if (FLAGS_sleep_ms > 0) {
      bthread_usleep(FLAGS_sleep_ms * 1000);
    }
  }

  LOG(INFO) << "Thread " << args->thread_id << " completed: " << success_count
            << "/" << FLAGS_operations_per_thread << " operations successful";

  return NULL;
}

// Simple config function
void get_config() {
  std::cout << CYAN << "=== Simple Multithreaded Couchbase Client ===" << RESET
            << std::endl;

  std::cout << "Username: ";
  std::cin >> g_config.username;
  std::cout << "Password: ";
  std::cin >> g_config.password;

  std::cout << "Enter 4 bucket names:" << std::endl;
  for (int i = 0; i < 4; ++i) {
    std::string bucket;
    std::cout << "Bucket " << (i + 1) << ": ";
    std::cin >> bucket;
    g_config.bucket_names.push_back(bucket);
  }

  int num_collections;
  std::cout << "Number of collections (0 for none): ";
  std::cin >> num_collections;

  for (int i = 0; i < num_collections; ++i) {
    std::string collection;
    std::cout << "Collection " << (i + 1) << ": ";
    std::cin >> collection;
    g_config.collection_names.push_back(collection);
  }
}

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::cout << GREEN << "Starting 16 bthreads (4 per bucket)" << RESET
            << std::endl;

  get_config();

  // Create bthreads
  std::vector<bthread_t> threads(NUM_THREADS);
  std::vector<ThreadArgs> args(NUM_THREADS);

  for (int i = 0; i < NUM_THREADS; ++i) {
    args[i].thread_id = i;
    args[i].bucket_id = i / THREADS_PER_BUCKET;
    args[i].bucket_name = g_config.bucket_names[args[i].bucket_id];

    if (bthread_start_background(&threads[i], NULL, thread_worker, &args[i]) !=
        0) {
      LOG(ERROR) << "Failed to create thread " << i;
      return -1;
    }

    bthread_usleep(50000);  // 50ms delay between thread starts
  }

  std::cout << "All 16 threads started!" << std::endl;

  // Wait for all threads
  for (int i = 0; i < NUM_THREADS; ++i) {
    bthread_join(threads[i], NULL);
  }

  std::cout << GREEN << "All threads completed!" << RESET << std::endl;
  return 0;
}
