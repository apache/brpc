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

#include <atomic>
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

const int NUM_THREADS = 20;
const int THREADS_PER_BUCKET = 5;

// Simple global config
struct {
  std::string username = "Administrator";
  std::string password = "password";
  std::vector<std::string> bucket_names = {"testing0", "testing1", "testing2",
                                           "testing3"};
} g_config;

// Simple thread statistics
struct ThreadStats {
  std::atomic<int> operations_attempted{0};
  std::atomic<int> operations_successful{0};
  std::atomic<int> operations_failed{0};

  void reset() {
    operations_attempted = 0;
    operations_successful = 0;
    operations_failed = 0;
  }
};

// Global statistics
struct GlobalStats {
  ThreadStats total;
  std::vector<ThreadStats> per_thread_stats;

  GlobalStats() : per_thread_stats(NUM_THREADS) {}

  void aggregate_stats() {
    total.reset();
    for (const auto& stats : per_thread_stats) {
      total.operations_attempted += stats.operations_attempted.load();
      total.operations_successful += stats.operations_successful.load();
      total.operations_failed += stats.operations_failed.load();
    }
  }
} g_stats;

// Simple thread arguments
struct ThreadArgs {
  int thread_id;
  int bucket_id;
  std::string bucket_name;
  ThreadStats* stats;
};

// Simple CRUD operations on default collection
void perform_crud_operations_default(brpc::CouchbaseOperations& couchbase_ops,
                                     const std::string& base_key,
                                     ThreadStats* stats) {
  std::string key = base_key + "_default";
  std::string value = butil::string_printf(
      R"({"thread_id": %d, "collection": "default"})", (int)bthread_self());

  stats->operations_attempted++;

  // UPSERT
  brpc::CouchbaseOperations::Result result = couchbase_ops.upsert(key, value);
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
    return;
  }

  stats->operations_attempted++;

  // GET
  result = couchbase_ops.get(key);
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
    return;
  }

  stats->operations_attempted++;

  // DELETE
  result = couchbase_ops.delete_(key);
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
  }
}

// Simple CRUD operations on col1 collection
void perform_crud_operations_col1(brpc::CouchbaseOperations& couchbase_ops,
                                  const std::string& base_key,
                                  ThreadStats* stats) {
  std::string key = base_key + "_col1";
  std::string value = butil::string_printf(
      R"({"thread_id": %d, "collection": "col1"})", (int)bthread_self());

  stats->operations_attempted++;

  // UPSERT
  brpc::CouchbaseOperations::Result result =
      couchbase_ops.upsert(key, value, "col1");
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
    return;
  }

  stats->operations_attempted++;

  // GET
  result = couchbase_ops.get(key, "col1");
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
    return;
  }

  stats->operations_attempted++;

  // DELETE
  result = couchbase_ops.delete_(key, "col1");
  if (result.success) {
    stats->operations_successful++;
  } else {
    stats->operations_failed++;
  }
}

// Simple thread worker function
void* thread_worker(void* arg) {
  ThreadArgs* args = static_cast<ThreadArgs*>(arg);

  std::cout << CYAN << "Thread " << args->thread_id << " starting on bucket "
            << args->bucket_name << RESET << std::endl;

  // Create CouchbaseOperations instance for this thread
  brpc::CouchbaseOperations couchbase_ops;

  // Authentication
  brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticate(
      g_config.username, g_config.password, "127.0.0.1:11210", false, "");

  if (!auth_result.success) {
    std::cout << RED << "Thread " << args->thread_id << ": Auth failed - "
              << auth_result.error_message << RESET << std::endl;
    return NULL;
  }

  // Select bucket
  brpc::CouchbaseOperations::Result bucket_result =
      couchbase_ops.selectBucket(args->bucket_name);

  if (!bucket_result.success) {
    std::cout << RED << "Thread " << args->thread_id
              << ": Bucket selection failed - " << bucket_result.error_message
              << RESET << std::endl;
    return NULL;
  }

  std::cout << GREEN << "Thread " << args->thread_id << " connected to bucket "
            << args->bucket_name << RESET << std::endl;

  // Perform operations - 10 times on default collection, 10 times on col1
  // collection
  for (int i = 0; i < 10; ++i) {
    std::string base_key =
        butil::string_printf("thread_%d_op_%d", args->thread_id, i);

    // CRUD operations on default collection
    perform_crud_operations_default(couchbase_ops, base_key, args->stats);

    // CRUD operations on col1 collection
    perform_crud_operations_col1(couchbase_ops, base_key, args->stats);

    // Small delay between operations
    bthread_usleep(10000);  // 10ms
  }

  int successful = args->stats->operations_successful.load();
  int attempted = args->stats->operations_attempted.load();
  int failed = args->stats->operations_failed.load();

  std::cout << GREEN << "Thread " << args->thread_id
            << " completed: " << successful << "/" << attempted
            << " operations successful, " << failed << " failed" << RESET
            << std::endl;

  return NULL;
}

// Print simple statistics
void print_stats() {
  g_stats.aggregate_stats();

  std::cout << std::endl;
  std::cout << CYAN << "=== TEST RESULTS ===" << RESET << std::endl;

  int total_attempted = g_stats.total.operations_attempted.load();
  int total_successful = g_stats.total.operations_successful.load();
  int total_failed = g_stats.total.operations_failed.load();

  double success_rate = total_attempted > 0
                            ? (double)total_successful / total_attempted * 100.0
                            : 0.0;

  std::cout << GREEN << "Overall Performance:" << RESET << std::endl;
  std::cout << "  Total Operations: " << total_attempted << std::endl;
  std::cout << "  Successful: " << total_successful << " (" << success_rate
            << "%)" << std::endl;
  std::cout << "  Failed: " << total_failed << std::endl;
  std::cout << std::endl;

  // Per-thread breakdown
  std::cout << YELLOW << "Per-Thread Performance:" << RESET << std::endl;
  for (int i = 0; i < NUM_THREADS; ++i) {
    const auto& stats = g_stats.per_thread_stats[i];
    int attempted = stats.operations_attempted.load();
    int successful = stats.operations_successful.load();
    int failed = stats.operations_failed.load();

    std::cout << "  Thread " << i << ": " << attempted << " ops, " << successful
              << " success, " << failed << " failed" << std::endl;
  }
  std::cout << std::endl;
}

int main(int argc, char* argv[]) {
  std::cout << GREEN << "Starting Simple Multithreaded Couchbase Client"
            << RESET << std::endl;
  std::cout
      << YELLOW
      << "20 threads: 5 per bucket (testing0, testing1, testing2, testing3)"
      << RESET << std::endl;
  std::cout << BLUE
            << "Each thread performs CRUD operations on default collection and "
               "col1 collection"
            << RESET << std::endl;

  // Create threads and arguments
  std::vector<bthread_t> threads(NUM_THREADS);
  std::vector<ThreadArgs> args(NUM_THREADS);

  // Assign threads to buckets
  for (int i = 0; i < NUM_THREADS; ++i) {
    args[i].thread_id = i;
    args[i].bucket_id = i / THREADS_PER_BUCKET;
    args[i].bucket_name = g_config.bucket_names[args[i].bucket_id];
    args[i].stats = &g_stats.per_thread_stats[i];
  }

  // Print thread assignments
  std::cout << "Thread Assignments:" << RESET << std::endl;
  for (int i = 0; i < NUM_THREADS; ++i) {
    std::cout << "Thread " << i << " -> Bucket: " << args[i].bucket_name
              << std::endl;
  }
  std::cout << std::endl;

  // Start all threads
  for (int i = 0; i < NUM_THREADS; ++i) {
    if (bthread_start_background(&threads[i], NULL, thread_worker, &args[i]) !=
        0) {
      std::cout << RED << "Failed to create thread " << i << RESET << std::endl;
      return -1;
    }
  }

  std::cout << GREEN << "All 20 threads started!" << RESET << std::endl;

  // Wait for all threads to complete
  std::cout << YELLOW << "Waiting for all threads to complete..." << RESET
            << std::endl;
  for (int i = 0; i < NUM_THREADS; ++i) {
    bthread_join(threads[i], NULL);
  }

  std::cout << GREEN << "All threads completed!" << RESET << std::endl;

  // Print statistics
  print_stats();

  return 0;
}
