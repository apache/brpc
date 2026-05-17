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

#include <iostream>
#include <string>

// ANSI color codes for console output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

int main() {
  // traditional bRPC Couchbase client
  brpc::Channel channel;
  brpc::ChannelOptions options;
  options.protocol = brpc::PROTOCOL_COUCHBASE;
  options.connection_type = "single";
  options.timeout_ms = 1000;  // 1 second
  options.max_retry = 3;
  if (channel.Init("localhost:11210", &options) != 0) {
    LOG(ERROR) << "Failed to initialize channel";
    return -1;
  }
  brpc::Controller cntl;
  brpc::CouchbaseOperations::CouchbaseRequest req;
  brpc::CouchbaseOperations::CouchbaseResponse res;
  uint64_t cas;
  req.authenticateRequest("Administrator", "password");
  channel.CallMethod(NULL, &cntl, &req, &res, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Unable to authenticate: Something went wrong"
               << cntl.ErrorText();
    return -1;
  } else {
    if (res.popHello(&cas) && res.popAuthenticate(&cas)) {
      std::cout << "Traditional bRPC Couchbase Client Authentication Successful"
                << std::endl;
    } else {
      std::cout << "Client Authentication Failed with status code: " << std::hex
                << res._status_code << std::endl;
      return -1;
    }
  }
  cntl.Reset();
  // clearing request and response

  req.Clear();
  res.Clear();
  req.selectBucketRequest("testing");
  channel.CallMethod(NULL, &cntl, &req, &res, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Unable to select bucket: Something went wrong"
               << cntl.ErrorText();
    return -1;
  } else {
    if (res.popSelectBucket(&cas)) {
      std::cout
          << "Traditional bRPC Couchbase Client Bucket Selection Successful"
          << std::endl;
    } else {
      // the status code will be updated only after you do
      // popFunctionName(param).
      std::cout << "Traditional bRPC Couchbase Client Bucket Selection Failed "
                   "with status code: "
                << std::hex << res._status_code << std::endl;
      std::cout << "Error Message: " << res.lastError() << std::endl;
      return -1;
    }
  }
  cntl.Reset();
  // clearing request and response

  req.Clear();
  res.Clear();
  req.addRequest(
      "sample_key",
      R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",
      0 /*flags*/, 0 /*exptime*/, 0 /*cas*/);
  channel.CallMethod(NULL, &cntl, &req, &res, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Unable to add key-value: Something went wrong"
               << cntl.ErrorText();
    return -1;
  } else {
    if (res.popAdd(&cas)) {
      std::cout
          << "Traditional bRPC Couchbase Client Key-Value Addition Successful"
          << std::endl;
    } else {
      // the status code will be updated only after you do
      // popFunctionName(param).
      std::cout << "Traditional bRPC Couchbase Client Key-Value Addition "
                   "Failed with status code: "
                << std::hex << res._status_code << std::endl;
      std::cout << "Error Message: " << res.lastError() << std::endl;
      return -1;
    }
  }
  cntl.Reset();

  // clearing request and response before doing a getRequest
  req.Clear();
  res.Clear();
  req.getRequest("sample_key");
  channel.CallMethod(NULL, &cntl, &req, &res, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Unable to get value for key: Something went wrong"
               << cntl.ErrorText();
    return -1;
  } else {
    std::string value;
    uint32_t flags;
    if (res.popGet(&value, &flags, &cas)) {
      std::cout
          << "Traditional bRPC Couchbase Client Key-Value Retrieval Successful"
          << std::endl;
      std::cout << "Retrieved Value: " << value << std::endl;
    } else {
      // note the status code will be updated only after you do
      //  popFunctionName(param).
      std::cout << "Traditional bRPC Couchbase Client Key-Value Retrieval "
                   "Failed with status code: "
                << std::hex << res._status_code << std::endl;
      std::cout << "Error Message: " << res.lastError() << std::endl;
      return -1;
    }
  }
  cntl.Reset();
  // clearing request and response before doing a deleteRequest

  req.Clear();
  res.Clear();
  req.deleteRequest("sample_key");
  channel.CallMethod(NULL, &cntl, &req, &res, NULL);
  if (cntl.Failed()) {
    LOG(ERROR) << "Unable to delete key-value: Something went wrong"
               << cntl.ErrorText();
    return -1;
  } else {
    if (res.popDelete()) {
      std::cout
          << "Traditional bRPC Couchbase Client Key-Value Deletion Successful"
          << std::endl;
    } else {
      // the status code will be updated only after you do
      // popFunctionName(param).
      std::cout << "Traditional bRPC Couchbase Client Key-Value Deletion "
                   "Failed with status code: "
                << std::hex << res._status_code << std::endl;
      std::cout << "Error Message: " << res.lastError() << std::endl;
      return -1;
    }
  }
  return 0;
}