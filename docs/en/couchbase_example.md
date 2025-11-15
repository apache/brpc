## Couchbase bRPC Binary Protocol Integration

This document explains the implementation of Couchbase Binary Protocol support added to bRPC, and the available high-level operations, collection support, SSL authentication, and how to run the provided example client against either a local Couchbase Server cluster or a Couchbase Capella (cloud) deployment. However, the couchbase binary protocol implementation in bRPC currently do not have fine-grained optimizations which has been already done in the couchbase-cxx-client SDK also having query support, better error handling and much more optimized/reliable operations. So, we also added the support of couchbase using couchbase-cxx-SDK in bRPC and is available at [Couchbaselabs-cb-brpc](https://github.com/couchbaselabs/cb_brpc/tree/couchbase_sdk_brpc).

---
### 1. Overview

The integration provides high-level APIs for communicating with Couchbase Server using its Binary Protocol, using the high-level `CouchbaseOperations` class which provides a simplified interface.

The core pieces are:
* `src/brpc/policy/couchbase_protocol.[h|cpp]` – framing + parse loop for binary responses, and request serialization.
* `src/brpc/couchbase.[h|cpp]` – high-level `CouchbaseOperations` class with request (`CouchbaseRequest`) and response (`CouchbaseResponse`) builders, parsers and error-handlers.
* `example/couchbase_c++/couchbase_client.cpp` – an end‑to‑end example using the high-level API for authentication, bucket selection, CRUD operations, and collection‑scoped operations.
* `example/couchbase_c++/multithreaded_couchbase_client.cpp` – a multithreaded example where an instance of `CouchbaseOperations` is shared across the threads operating on same bucket. An another block of code where multiple threads have their own `CouchbaseOperations` instance as the threads operate on different buckets.
* `example/couchbase_c++/traditional_brpc_couchbase_client.cpp` – demonstrates the traditional bRPC approach with manual channel, controller, and request/response management for advanced users who need fine-grained control.

Design goals:
* **SSL Support**: Built-in SSL/TLS support for secure connections to Couchbase Capella.
* **Per-instance Authentication**: Each `CouchbaseOperations` object maintains its own authenticated session if each instance connects to a different bucket, when multiple instances connect/operate on the same bucket then a single TCP socket is shared for these `CouchbaseOperations` instances because separate `connection_groups` are created on the basis of `server_name+bucket`.
* **Collection Support**: Native support for collection-scoped operations.
* Keep wire structs identical to the binary protocol (24‑byte header, network order numeric fields).
* Future extensions for advanced features.

---
### 2. Features

| Category | Supported Operations | Notes |
|----------|----------------------|-------|
| **High-Level API** | `CouchbaseOperations` class | **Recommended**: Simple methods returning `Result` struct |
| **Traditional API** | Manual channel/controller management | **Advanced**: Direct bRPC access for custom configurations |
| **SSL/TLS Support** | Built-in SSL encryption | **Required** for Couchbase Capella, optional for local clusters |
| Authentication | SASL `PLAIN` with/without SSL | `authenticate()` for non-SSL, `authenticateSSL()` for SSL connections |
| Bucket selection | Integrated with authentication | Bucket specified during authentication; `selectBucket()` also available separately |
| Basic KV | `add()`, `upsert()`, `delete_()`, `get()` | Clean API with `Result` struct error handling; |
| **Pipeline Operations** | `beginPipeline()`, `pipelineRequest()`, `executePipeline()` | **NEW**: Batch multiple operations in single network call for improved performance |
| Collections | Collection-scoped CRUD operations | Pass collection name as optional parameter (defaults to "_default") |
| Error Handling | `Result.success` + `Result.error_message` + `Result.status_code` | Human-readable error messages with Couchbase status codes |

- **Simplified**: No need to manage channels, controllers, or response parsing
- **Flexible Threading**: Share instances across threads for same bucket/server, or create separate instances for different buckets/servers
- **Error Handling**: Simple boolean success with descriptive error messages and error codes
- **SSL Built-in**: SSL handling for secure connections


---
### 3. Binary Protocol Mapping

Couchbase binary protcol header, for original documentation [click here](https://github.com/couchbase/kv_engine/blob/master/docs/BinaryProtocol.md). The following header format has been used to connect with the couchbase servers. 
```
Byte/     0       |       1       |       2       |       3       |
     /              |               |               |               |
    |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
    +---------------+---------------+---------------+---------------+
   0| Magic         | Opcode        | Key length                    |
    +---------------+---------------+---------------+---------------+
   4| Extras length | Data type     | vbucket id                    |
    +---------------+---------------+---------------+---------------+
   8| Total body length                                             |
    +---------------+---------------+---------------+---------------+
  12| Opaque                                                        |
    +---------------+---------------+---------------+---------------+
  16| CAS                                                           |
    |                                                               |
    +---------------+---------------+---------------+---------------+
    Total 24 bytes
```

Overall packet structure:-
```
  Byte/     0       |       1       |       2       |       3       |
     /              |               |               |               |
    |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
    +---------------+---------------+---------------+---------------+
   0| HEADER                                                        |
    |                                                               |
    |                                                               |
    |                                                               |
    +---------------+---------------+---------------+---------------+
  24| COMMAND-SPECIFIC EXTRAS (as needed)                           |
    |  (note length in the extras length header field)              |
    +---------------+---------------+---------------+---------------+
   m| Key (as needed)                                               |
    |  (note length in key length header field)                     |
    +---------------+---------------+---------------+---------------+
   n| Value (as needed)                                             |
    |  (note length is total body length header field, minus        |
    |   sum of the extras and key length body fields)               |
    +---------------+---------------+---------------+---------------+
    Total 24 + x bytes (24 byte header, and x byte body)
```

---
### 4. High-Level API (`CouchbaseOperations`)

**Approach**: Use the `CouchbaseOperations` class for operations. Instances can be shared across threads when connecting to the same bucket, or you can create separate instances in multi-threading where each thread is connecting to a separate bucket.

#### Basic Usage:
```cpp
#include <brpc/couchbase.h>

brpc::CouchbaseOperations couchbase_ops;

// 1. Authenticate with bucket selection (REQUIRED for each instance)
brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticate(
    username, password, server_address, bucket_name);
if (!auth_result.success) {
    LOG(ERROR) << "Auth failed: " << auth_result.error_message;
    return -1;
}

// 2. Perform operations (bucket is already selected during authentication)
brpc::CouchbaseOperations::Result add_result = couchbase_ops.add("user::123", json_value);
if (add_result.success) {
    std::cout << "Document added successfully!" << std::endl;
} else {
    std::cout << "Add failed: " << add_result.error_message << std::endl;
}

// Optional: Switch to a different bucket (if needed)
// brpc::CouchbaseOperations::Result bucket_result = couchbase_ops.selectBucket("another_bucket");
```

#### SSL Authentication (Essential for Couchbase Capella):
To know how to download the security certificate [click here](https://docs.couchbase.com/cloud/security/security-certificates.html).
```cpp
// For Couchbase Capella (cloud) - SSL is REQUIRED
brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticateSSL(
    username, 
    password, 
    "cluster.cloud.couchbase.com:11207",  // SSL port
    bucket_name,                          // bucket name
    "path/to/certificate.txt"             // certificate path(can be downloaded from capella UI)
);
```

#### Collection Operations:
```cpp
// Default collection
auto result = couchbase_ops.get("doc::1");

// Specific collection
auto result = couchbase_ops.get("doc::1", "my_collection");
auto add_result = couchbase_ops.add("doc::2", value, "my_collection");
```

#### Pipeline Operations (Performance Optimization):
The pipeline API allows batching multiple operations into a single network call, significantly improving performance for bulk operations:

#### How Pipeline Operations Work

1. **Begin Pipeline**: Start a new pipeline session
2. **Add Operations**: Queue multiple operations without executing them
3. **Execute Pipeline**: Send all operations in a single network call
4. **Process Results**: Handle results in the same order as requests

#### Pipeline API Methods

| Method | Description | Usage |
|--------|-------------|-------|
| `beginPipeline()` | Start a new pipeline session | Must call before adding operations |
| `pipelineRequest(op_type, key, value, collection)` | Add operation to pipeline | Supports all CRUD operations |
| `executePipeline()` | Execute all queued operations | Returns `vector<Result>` in request order |
| `clearPipeline()` | Clear pipeline without executing | Use for cleanup on errors |
| `isPipelineActive()` | Check if pipeline is active | Returns `bool` |
| `getPipelineSize()` | Get number of queued operations | Returns `size_t` |

```cpp
// Begin a new pipeline
if (!couchbase_ops.beginPipeline()) {
    LOG(ERROR) << "Failed to begin pipeline";
    return -1;
}

// Add multiple operations to the pipeline (not executed yet)
bool success = true;
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "key1", "value1");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::UPSERT, "key2", "value2");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "key1");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE, "key3");

if (!success) {
    couchbase_ops.clearPipeline();  // Clean up on error
    return -1;
}

// Execute all operations in a single network call
std::vector<brpc::CouchbaseOperations::Result> results = couchbase_ops.executePipeline();

// Process results in the same order as requests
for (size_t i = 0; i < results.size(); ++i) {
    if (results[i].success) {
        std::cout << "Operation " << i << " succeeded" << std::endl;
        if (!results[i].value.empty()) {
            std::cout << "Value: " << results[i].value << std::endl;
        }
    } else {
        std::cout << "Operation " << i << " failed: " << results[i].error_message << std::endl;
    }
}
```

**Pipeline with Collections**:
```cpp
// Pipeline operations can also use collections
couchbase_ops.beginPipeline();
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "doc1", "value1", "my_collection");
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "doc1", "", "my_collection");
auto results = couchbase_ops.executePipeline();
```

#### Error Handling Pattern:
```cpp
brpc::CouchbaseOperations::Result result = couchbase_ops.someOperation(...);
if (!result.success) {
    // Handle error
    LOG(ERROR) << "Operation failed: " << result.error_message;
    LOG(ERROR) << "Error Code: " << result.status_code;  // Couchbase status code
} else {
    // Use result.value if applicable (for Get operations)
    std::cout << "Retrieved value: " << result.value << std::endl;
}
```

---
### 5. Traditional bRPC Couchbase Client (`traditional_brpc_couchbase_client.cpp`)

For developers who need fine-grained control over the bRPC framework or want to understand the low-level implementation, we provide a traditional bRPC client example. This approach requires manual management of channels, controllers, and response parsing.

**When to use Traditional API:**
- Advanced bRPC users who need custom channel configurations
- Fine-grained control over connection pooling and retry logic
- Direct access to underlying bRPC controller for debugging
- Learning the internal workings of the high-level API

**When to use High-Level API (Recommended):**
- Standard CRUD operations and authentication
- Simpler error handling and cleaner code
- Collection based operations with minimal boilerplate
- Pipeline operations for batch processing while also available in traditional approach it is easier to do using High-Level API.

#### Traditional Client Example Walkthrough

The traditional client (`example/couchbase_c++/traditional_brpc_couchbase_client.cpp`) demonstrates the low-level bRPC approach:

**1. Channel Setup and Configuration**
```cpp
brpc::Channel channel;
brpc::ChannelOptions options;
options.protocol = brpc::PROTOCOL_COUCHBASE;    // Set Couchbase protocol
options.connection_type = "single";              // Single persistent connection
options.timeout_ms = 1000;                       // 1 second timeout
options.max_retry = 3;                          // Retry up to 3 times

if (channel.Init("localhost:11210", &options) != 0) {
    LOG(ERROR) << "Failed to initialize channel";
    return -1;
}
```

**2. Authentication with Manual Request/Response Handling**
```cpp
brpc::Controller cntl;
brpc::CouchbaseOperations::CouchbaseRequest req;
brpc::CouchbaseOperations::CouchbaseResponse res;
uint64_t cas;

// Build authentication request
req.authenticateRequest("Administrator", "password");

// Execute the request
channel.CallMethod(NULL, &cntl, &req, &res, NULL);

// Check controller status
if (cntl.Failed()) {
    LOG(ERROR) << "Unable to authenticate: " << cntl.ErrorText();
    return -1;
}

// Parse response - must call popHello() and popAuthenticate() in order
if (res.popHello(&cas) && res.popAuthenticate(&cas)) {
    std::cout << "Authentication Successful" << std::endl;
} else {
    std::cout << "Authentication Failed with status code: " 
              << std::hex << res._status_code << std::endl;
    return -1;
}
```

**3. Bucket Selection**
```cpp
// IMPORTANT: Reset controller and clear request/response before each operation
cntl.Reset();
req.Clear();
res.Clear();

// Build bucket selection request
req.selectBucketRequest("testing");

// Execute the request
channel.CallMethod(NULL, &cntl, &req, &res, NULL);

if (cntl.Failed()) {
    LOG(ERROR) << "Unable to select bucket: " << cntl.ErrorText();
    return -1;
}

// Parse response - status_code only updated AFTER calling pop function
if (res.popSelectBucket(&cas)) {
    std::cout << "Bucket Selection Successful" << std::endl;
} else {
    std::cout << "Bucket Selection Failed with status code: " 
              << std::hex << res._status_code << std::endl;
    std::cout << "Error Message: " << res.lastError() << std::endl;
    return -1;
}
```

**4. ADD Operation (Create Document)**
```cpp
// Reset for new operation
cntl.Reset();
req.Clear();
res.Clear();

// Build ADD request
req.addRequest(
    "sample_key",                                           // key
    R"({"name": "John Doe", "age": 30, "email": "john@example.com"})",  // value
    0,      // flags
    0,      // exptime (0 = no expiration)
    0       // cas (0 for new document)
);

// Execute the request
channel.CallMethod(NULL, &cntl, &req, &res, NULL);

if (cntl.Failed()) {
    LOG(ERROR) << "Unable to add key-value: " << cntl.ErrorText();
    return -1;
}

// Parse response
if (res.popAdd(&cas)) {
    std::cout << "Key-Value Addition Successful" << std::endl;
} else {
    std::cout << "Key-Value Addition Failed with status code: " 
              << std::hex << res._status_code << std::endl;
    std::cout << "Error Message: " << res.lastError() << std::endl;
    return -1;
}
```

**5. GET Operation (Retrieve Document)**
```cpp
// Reset for new operation
cntl.Reset();
req.Clear();
res.Clear();

// Build GET request
req.getRequest("sample_key");

// Execute the request
channel.CallMethod(NULL, &cntl, &req, &res, NULL);

if (cntl.Failed()) {
    LOG(ERROR) << "Unable to get value for key: " << cntl.ErrorText();
    return -1;
}

// Parse response - GET returns value and flags
std::string value;
uint32_t flags;
if (res.popGet(&value, &flags, &cas)) {
    std::cout << "Key-Value Retrieval Successful" << std::endl;
    std::cout << "Retrieved Value: " << value << std::endl;
} else {
    std::cout << "Key-Value Retrieval Failed with status code: " 
              << std::hex << res._status_code << std::endl;
    std::cout << "Error Message: " << res.lastError() << std::endl;
    return -1;
}
```

**6. DELETE Operation (Remove Document)**
```cpp
// Reset for new operation
cntl.Reset();
req.Clear();
res.Clear();

// Build DELETE request
req.deleteRequest("sample_key");

// Execute the request
channel.CallMethod(NULL, &cntl, &req, &res, NULL);

if (cntl.Failed()) {
    LOG(ERROR) << "Unable to delete key-value: " << cntl.ErrorText();
    return -1;
}

// Parse response
if (res.popDelete()) {
    std::cout << "Key-Value Deletion Successful" << std::endl;
} else {
    std::cout << "Key-Value Deletion Failed with status code: " 
              << std::hex << res._status_code << std::endl;
    std::cout << "Error Message: " << res.lastError() << std::endl;
    return -1;
}
```

#### Key Differences: Traditional vs High-Level API

| Aspect | Traditional API | High-Level API |
|--------|----------------|----------------|
| **Setup** | Manual channel, controller, request/response management | Single `CouchbaseOperations` instance |
| **Error Handling** | Check both `cntl.Failed()` and response status | Simple `Result.success` boolean |
| **Resource Management** | Must call `cntl.Reset()`, `req.Clear()`, `res.Clear()` | Automatic |
| **Response Parsing** | Manual `pop*()` calls with CAS handling | Transparent |
| **Code Verbosity** | ~15-20 lines per operation | ~2-3 lines per operation |
| **Collections** | Manual collection ID retrieval and management | Automatic with collection name parameter |
| **Pipeline Operations** | Complex manual request building | Simple `beginPipeline()`, `pipelineRequest()`, `executePipeline()` |
| **SSL Support** | Manual SSL configuration in channel options | Built-in `authenticateSSL()` method |
| **Threading** | Manual connection pooling management | Automatic connection group management |

---
### 6. Request/Response Classes (`CouchbaseRequest`/`CouchbaseResponse`)

These classes are public in `CouchbaseOperations` and can be used for advanced bRPC programs. The high-level API uses these classes internally, and the traditional client example demonstrates their direct usage. They are responsible for building the request that needs to be sent and received over the channel.


#### Response Parsing:
Each `pop*` method consumes the front of the internal response buffer, validating:
1. Header present.
2. Opcode matches expected operation.
3. Status == success (otherwise `_err` filled with formatted message).
4. Body length sufficient.

---
### 7. Example Client Walkthrough

#### Single-Threaded Example (`couchbase_client.cpp`)
Uses the **high-level `CouchbaseOperations` API**:

1. **Create `CouchbaseOperations` instance** - can create more than one per thread.
```cpp
brpc::CouchbaseOperations couchbase_ops;
```

2. **Prompt for credentials** - username/password for authentication.
```cpp
std::string username = "Administrator";
std::string password = "password";
while (username.empty() || password.empty()) {
    std::cout << "Enter Couchbase username: ";
    std::cin >> username;
    std::cout << "Enter Couchbase password: ";
    std::cin >> password;
}
```

3. **Authentication with bucket selection** - `authenticate()` for local, `authenticateSSL()` for Capella.

**Function Signatures:**
```cpp
// Non-SSL authentication
Result authenticate(const string& username,     // Couchbase username 
                   const string& password,     // Couchbase password
                   const string& server_address, // Server host:port (e.g., "localhost:11210")
                   const string& bucket_name);   // Target bucket name

// SSL authentication  
Result authenticateSSL(const string& username,     // Couchbase username
                      const string& password,     // Couchbase password  
                      const string& server_address, // Server host:port (e.g., "cluster.cloud.couchbase.com:11207")
                      const string& bucket_name,   // Target bucket name
                      string path_to_cert);        // Path to SSL certificate file
```

**Usage Examples:**
```cpp
// For local Couchbase (non-SSL)
brpc::CouchbaseOperations::Result auth_result = 
    couchbase_ops.authenticate(username, password, FLAGS_server, "testing");

// For Couchbase Capella (SSL)
// brpc::CouchbaseOperations::Result auth_result = 
//     couchbase_ops.authenticateSSL(username, password, "cluster.cloud.couchbase.com:11207", 
//                                   "bucket_name", "path/to/cert.txt");

if (!auth_result.success) {
    LOG(ERROR) << "Authentication failed: " << auth_result.error_message;
    return -1;
}
```

4. **Basic CRUD operations**:
   - Add document (should succeed)
   - Try adding same key again (should fail with "key exists")
   - Get document (retrieve the added document)

**Function Signatures:**
```cpp
// ADD operation - creates new document, fails if key exists
Result add(const string& key,                    // Document key/ID
          const string& value,                  // Document value (JSON string)
          string collection_name = "_default"); // Collection name (optional, defaults to "_default")

// GET operation - retrieves document by key
Result get(const string& key,                    // Document key/ID to retrieve
          string collection_name = "_default"); // Collection name (optional, defaults to "_default")
```

**Usage Examples:**
```cpp
std::string add_key = "user::test_brpc_binprot";
std::string add_value = R"({"name": "John Doe", "age": 30, "email": "john@example.com"})";

// First ADD operation (should succeed)
brpc::CouchbaseOperations::Result add_result = couchbase_ops.add(add_key, add_value);
if (add_result.success) {
    std::cout << "ADD operation successful" << std::endl;
} else {
    std::cout << "ADD operation failed: " << add_result.error_message << std::endl;
}

// Second ADD operation (should fail - key exists)
brpc::CouchbaseOperations::Result add_result2 = couchbase_ops.add(add_key, add_value);
if (!add_result2.success) {
    std::cout << "Second ADD failed as expected: " << add_result2.error_message << std::endl;
}

// GET operation
brpc::CouchbaseOperations::Result get_result = couchbase_ops.get(add_key);
if (get_result.success) {
    std::cout << "GET operation successful" << std::endl;
    std::cout << "GET value: " << get_result.value << std::endl;
}
```

5. **Multiple document operations** - Add several documents with different keys.
```cpp
std::string item1_key = "binprot_item1";
std::string item2_key = "binprot_item2";
std::string item3_key = "binprot_item3";

couchbase_ops.add(item1_key, add_value);
couchbase_ops.add(item2_key, add_value);
couchbase_ops.add(item3_key, add_value);
```

6. **Upsert operations**:
   - Upsert existing document (should update)
   - Upsert new document (should create)
   - Verify with Get operations

**Function Signature:**
```cpp
// UPSERT operation - creates new document or updates existing one
Result upsert(const string& key,                    // Document key/ID
             const string& value,                  // Document value (JSON string)
             string collection_name = "_default"); // Collection name (optional, defaults to "_default")
```

**Usage Examples:**
```cpp
std::string upsert_key = "upsert_test";
std::string upsert_value = R"({"operation": "upsert", "version": 1})";

// Upsert new document (will create)
brpc::CouchbaseOperations::Result upsert_result = couchbase_ops.upsert(upsert_key, upsert_value);

// Upsert existing document (will update)
std::string updated_value = R"({"operation": "upsert", "version": 2})";
brpc::CouchbaseOperations::Result update_result = couchbase_ops.upsert(upsert_key, updated_value);

// Verify with GET
brpc::CouchbaseOperations::Result check_result = couchbase_ops.get(upsert_key);
```

7. **Delete operations**:
   - Delete non-existent key (should fail gracefully)
   - Delete existing key (should succeed)

**Function Signature:**
```cpp
// DELETE operation - removes document by key
Result delete_(const string& key,                   // Document key/ID to delete
              string collection_name = "_default"); // Collection name (optional, defaults to "_default")
```

**Usage Examples:**
```cpp
// Delete non-existent key
std::string delete_key = "non_existent_key";
brpc::CouchbaseOperations::Result delete_result = couchbase_ops.delete_(delete_key);
if (!delete_result.success) {
    std::cout << "Delete failed as expected: " << delete_result.error_message << std::endl;
}

// Delete existing key
std::string delete_existing_key = "binprot_item1";
brpc::CouchbaseOperations::Result delete_existing_result = couchbase_ops.delete_(delete_existing_key);
if (delete_existing_result.success) {
    std::cout << "Delete existing key successful" << std::endl;
}
```

8. **Collection-scoped operations** - Add/Get/Upsert/Delete in specific collections.

**Note:** All CRUD operations support an optional collection parameter. When not specified, operations default to the "_default" collection.

**Usage Examples:**
```cpp
std::string collection_name = "testing_collection";  // Target collection name
std::string coll_key = "collection::doc1";           // Document key
std::string coll_value = R"({"collection_operation": "add", "scope": "custom"})";  // Document value

// Collection-scoped ADD (key, value, collection_name)
brpc::CouchbaseOperations::Result coll_add_result = 
    couchbase_ops.add(coll_key, coll_value, collection_name);

// Collection-scoped GET (key, collection_name)
brpc::CouchbaseOperations::Result coll_get_result = 
    couchbase_ops.get(coll_key, collection_name);

// Collection-scoped UPSERT (key, value, collection_name)
brpc::CouchbaseOperations::Result coll_upsert_result = 
    couchbase_ops.upsert(coll_key, coll_value, collection_name);

// Collection-scoped DELETE (key, collection_name)
brpc::CouchbaseOperations::Result coll_delete_result = 
    couchbase_ops.delete_(coll_key, collection_name);
```

9. **Pipeline operations demo**:
   - Begin pipeline and add multiple operations
   - Execute batch operations in single network call
   - Process results in order
   - Collection-scoped pipeline operations
   - Error handling and cleanup

**Function Signatures:**
```cpp
// Pipeline management functions
bool beginPipeline();                               // Start a new pipeline session

bool pipelineRequest(operation_type op_type,        // Operation type (ADD, UPSERT, GET, DELETE, etc.)
                    const string& key,             // Document key/ID
                    const string& value = "",       // Document value (empty for GET/DELETE operations)
                    string collection_name = "_default"); // Collection name (optional)

vector<Result> executePipeline();                   // Execute all queued operations and return results

bool clearPipeline();                              // Clear pipeline without executing (cleanup)

// Pipeline status functions
bool isPipelineActive() const;                      // Check if pipeline is active
size_t getPipelineSize() const;                    // Get number of queued operations
```

**Usage Examples:**
```cpp
// Begin pipeline
if (!couchbase_ops.beginPipeline()) {
    std::cout << "Failed to begin pipeline" << std::endl;
    return -1;
}

// Add multiple operations to pipeline
std::string pipeline_key1 = "pipeline::doc1";
std::string pipeline_key2 = "pipeline::doc2";
std::string pipeline_value1 = R"({"operation": "pipeline_add", "id": 1})";
std::string pipeline_value2 = R"({"operation": "pipeline_upsert", "id": 2})";

bool pipeline_success = true;
// pipelineRequest(operation_type, key, value, collection_name)
pipeline_success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, pipeline_key1, pipeline_value1);
pipeline_success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::UPSERT, pipeline_key2, pipeline_value2);
pipeline_success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, pipeline_key1);  // Empty value for GET

if (!pipeline_success) {
    couchbase_ops.clearPipeline();  // Clean up on error
    return -1;
}

// Execute pipeline - returns results in same order as requests
std::vector<brpc::CouchbaseOperations::Result> pipeline_results = couchbase_ops.executePipeline();

// Process results
for (size_t i = 0; i < pipeline_results.size(); ++i) {
    if (pipeline_results[i].success) {
        std::cout << "Operation " << (i + 1) << " SUCCESS";
        if (!pipeline_results[i].value.empty()) {
            std::cout << " - Value: " << pipeline_results[i].value;
        }
        std::cout << std::endl;
    } else {
        std::cout << "Operation " << (i + 1) << " FAILED: " 
                  << pipeline_results[i].error_message << std::endl;
    }
}

// Collection-scoped pipeline operations
if (couchbase_ops.beginPipeline()) {
    // pipelineRequest(operation_type, key, value, collection_name)
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "coll_pipeline::doc1", 
                                  R"({"collection_operation": "pipeline_add", "id": 1})", collection_name);
    couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "coll_pipeline::doc1", "", collection_name);
    auto coll_results = couchbase_ops.executePipeline();
}
```

10. **Bucket switching** - Demonstrate changing bucket selection.

**Function Signature:**
```cpp
// SELECTBUCKET operation - switch to a different bucket on the same server
Result selectBucket(const string& bucket_name);    // Target bucket name to switch to
```

**Usage Example:**
```cpp
std::string bucket_name = "testing";
std::cout << "Enter Couchbase bucket name: ";
std::cin >> bucket_name;

// selectBucket(bucket_name) - switches to the specified bucket
brpc::CouchbaseOperations::Result bucket_result = couchbase_ops.selectBucket(bucket_name);
if (!bucket_result.success) {
    LOG(ERROR) << "Bucket selection failed: " << bucket_result.error_message;
    return -1;
} else {
    std::cout << "Bucket Selection Successful" << std::endl;
}

// Perform operations on new bucket
performOperations(couchbase_ops);
```

#### Multithreaded Example (`multithreaded_couchbase_client.cpp`)
Demonstrates:
- **20 bthreads** (5 threads per bucket across 4 buckets)
- **Multiple threading patterns**: Each thread can create its own instance or share instances
- **Concurrent operations** across multiple buckets and collections
- **Thread-safe statistics tracking** for operations
- **Collection-scoped operations** across threads

**Global Configuration**:
```cpp
const int NUM_THREADS = 20;
const int THREADS_PER_BUCKET = 5;

// Global config structure
struct {
    std::string username = "Administrator";
    std::string password = "password";
    std::vector<std::string> bucket_names = {"t0", "t1", "t2", "t3"};
} g_config;

// Thread statistics tracking
struct ThreadStats {
    std::atomic<int> operations_attempted{0};
    std::atomic<int> operations_successful{0};
    std::atomic<int> operations_failed{0};
};

struct GlobalStats {
    ThreadStats total;
    std::vector<ThreadStats> per_thread_stats;
    GlobalStats() : per_thread_stats(NUM_THREADS) {}
} g_stats;
```

**Thread Worker Function**:
```cpp
struct ThreadArgs {
    int thread_id;
    int bucket_id;
    std::string bucket_name;
    ThreadStats* stats;
};

void* thread_worker(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);
    
    // Create CouchbaseOperations instance for this thread
    brpc::CouchbaseOperations couchbase_ops;
    
    // Authentication with assigned bucket
    brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticate(
        g_config.username, g_config.password, "127.0.0.1:11210", args->bucket_name);
    
    // For SSL authentication:
    // brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticateSSL(
    //     g_config.username, g_config.password, "127.0.0.1:11207", args->bucket_name, "/path/to/cert.txt");
    
    if (!auth_result.success) {
        std::cout << "Thread " << args->thread_id << ": Auth failed - " 
                  << auth_result.error_message << std::endl;
        return NULL;
    }
    
    // Perform CRUD operations on default collection
    std::string base_key = "thread_" + std::to_string(args->thread_id);
    perform_crud_operations_default(couchbase_ops, base_key, args->stats);
    
    // Perform collection-scoped operations
    perform_crud_operations_collection(couchbase_ops, base_key, "my_collection", args->stats);
    
    return NULL;
}
```

**CRUD Operations Functions**:
```cpp
void perform_crud_operations_default(brpc::CouchbaseOperations& couchbase_ops,
                                   const std::string& base_key, ThreadStats* stats) {
    std::string key = base_key + "_default";
    std::string value = R"({"thread_id": %d, "collection": "default"})";
    
    stats->operations_attempted++;
    
    // UPSERT operation
    brpc::CouchbaseOperations::Result result = couchbase_ops.upsert(key, value);
    if (result.success) {
        stats->operations_successful++;
    } else {
        stats->operations_failed++;
    }
    
    // GET operation
    stats->operations_attempted++;
    result = couchbase_ops.get(key);
    if (result.success) {
        stats->operations_successful++;
    } else {
        stats->operations_failed++;
    }
    
    // DELETE operation
    stats->operations_attempted++;
    result = couchbase_ops.delete_(key);
    if (result.success) {
        stats->operations_successful++;
    } else {
        stats->operations_failed++;
    }
}

void perform_crud_operations_collection(brpc::CouchbaseOperations& couchbase_ops,
                                      const std::string& base_key,
                                      const std::string& collection_name,
                                      ThreadStats* stats) {
    std::string key = base_key + "_collection";
    std::string value = R"({"thread_id": %d, "collection": ")" + collection_name + R"("})";
    
    // Collection-scoped operations
    stats->operations_attempted++;
    brpc::CouchbaseOperations::Result result = couchbase_ops.upsert(key, value, collection_name);
    if (result.success) {
        stats->operations_successful++;
    } else {
        stats->operations_failed++;
    }
    
    stats->operations_attempted++;
    result = couchbase_ops.get(key, collection_name);
    if (result.success) {
        stats->operations_successful++;
    } else {
        stats->operations_failed++;
    }
}
```

**Main Function - Thread Management**:
```cpp
int main(int argc, char* argv[]) {
    std::vector<bthread_t> threads(NUM_THREADS);
    std::vector<ThreadArgs> thread_args(NUM_THREADS);
    
    // Create threads - 5 threads per bucket across 4 buckets
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_args[i].thread_id = i;
        thread_args[i].bucket_id = i / THREADS_PER_BUCKET;
        thread_args[i].bucket_name = g_config.bucket_names[thread_args[i].bucket_id];
        thread_args[i].stats = &g_stats.per_thread_stats[i];
        
        if (bthread_start_background(&threads[i], NULL, thread_worker, &thread_args[i]) != 0) {
            LOG(ERROR) << "Failed to create thread " << i;
            return -1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; ++i) {
        bthread_join(threads[i], NULL);
    }
    
    // Aggregate and display statistics
    g_stats.aggregate_stats();
    std::cout << "Total operations attempted: " << g_stats.total.operations_attempted.load() << std::endl;
    std::cout << "Total operations successful: " << g_stats.total.operations_successful.load() << std::endl;
    std::cout << "Total operations failed: " << g_stats.total.operations_failed.load() << std::endl;
    
    return 0;
}
```

**Alternative Pattern - Shared Instance Demo**:
```cpp
// Shared instance worker function
void* shared_object_thread_worker(void *arg) {
    ThreadArgs* shared_args = static_cast<ThreadArgs*>(arg);
    brpc::CouchbaseOperations* shared_couchbase_ops = shared_args->couchbase_ops;
    
    // Perform operations - 10 times on default collection, 10 times on col1 collection
    for (int i = 0; i < 10; ++i) {
        std::string base_key = butil::string_printf("shared_thread_op_%d_thread_id_%d", 
                                                   i, shared_args->thread_id);
        
        // CRUD operations on default collection using shared instance
        perform_crud_operations_default(*shared_couchbase_ops, base_key, shared_args->stats);
        
        // CRUD operations on col1 collection using shared instance
        perform_crud_operations_col1(*shared_couchbase_ops, base_key, shared_args->stats);
        
        // Small delay between operations
        bthread_usleep(10000);  // 10ms
    }
    return NULL;
}

// Main function demonstrates shared instance pattern
int main_shared_demo() {
    // Create a shared CouchbaseOperations instance
    brpc::CouchbaseOperations shared_couchbase_ops;
    brpc::CouchbaseOperations::Result result;
    
    // Authenticate shared instance
    result = shared_couchbase_ops.authenticate(
        g_config.username, g_config.password, "127.0.0.1:11210", "t0");
    
    if (result.success) {
        std::cout << GREEN << "Shared CouchbaseOperations instance authenticated successfully!" 
                  << RESET << std::endl;
    } else {
        std::cout << RED << "Shared CouchbaseOperations instance authentication failed: " 
                  << result.error_message << RESET << std::endl;
        return -1;
    }
    
    // Configure all threads to use the shared instance
    std::vector<bthread_t> threads(NUM_THREADS);
    std::vector<ThreadArgs> args(NUM_THREADS);
    
    for (int i = 0; i < NUM_THREADS; ++i) {
        args[i].thread_id = i;
        args[i].couchbase_ops = &shared_couchbase_ops;  // Point to shared instance
        args[i].bucket_id = 0;
        args[i].bucket_name = "t0";  // All threads use same bucket via shared instance
        args[i].stats = &g_stats.per_thread_stats[i];
    }
    
    // Start all threads using shared instance
    for (int i = 0; i < NUM_THREADS; ++i) {
        if (bthread_start_background(&threads[i], NULL, shared_object_thread_worker, &args[i]) != 0) {
            std::cout << RED << "Failed to create shared object thread " << i << RESET << std::endl;
            return -1;
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < NUM_THREADS; ++i) {
        bthread_join(threads[i], NULL);
    }
    
    std::cout << GREEN << "All shared object threads completed!" << RESET << std::endl;
    return 0;
}
```

Key features:
- Demonstrates different connection patterns for multithreaded scenarios
- Shows concurrent access to different buckets and collections
- Proper resource management in multithreaded environments
- Statistics tracking across all threads
- Both separate instance and shared instance patterns

---
### 8. Building and Running the Examples

#### Build both examples:
```bash
cd example/couchbase_c++/
make
```

#### Run Single-Threaded Example (High-Level API):
```bash
./couchbase_client
```

#### Run Multithreaded Example (High-Level API):
```bash
./multithreaded_couchbase_client
```

#### Run Traditional bRPC Client (Low-Level API):
```bash
./traditional_brpc_couchbase_client
```

---
### 9. Setting Up Couchbase

#### A. Local Install (Non‑Docker)
Download from: https://www.couchbase.com/downloads/ (Community or Enterprise) and Install.

Setup steps:
- Open http://localhost:8091 in a browser and follow setup wizard
- Set admin credentials (Administrator / password)
- Accept terms, choose services (Data, Query, Index at minimum)
- Initialize cluster
- Create a bucket (e.g. travel-sample or custom)

Create collections (7.0+):
- Navigate: Buckets → Your Bucket → Scopes & Collections
- Add a Scope (optional) or use `_default`
- Add a Collection (e.g. `testing_collection`)

**SSL Configuration (Optional for Local)**:
```cpp
// Local without SSL - authenticate with bucket selection
auto result = couchbase_ops.authenticate(username, password, "localhost:11210", bucket_name);
```

#### B. Couchbase Capella (Cloud) - **SSL Required**
1. Sign up / log in: https://cloud.couchbase.com/
2. Create a Free Trial or Hosted Cluster
3. Create a bucket (or load sample dataset)
4. **Create database access credentials** with appropriate RBAC roles:
   - Data Reader/Writer (minimum)
   - Bucket Admin (for bucket operations)
5. **Download SSL Certificate**:
   - Go to Cluster → Connect → Download Certificate
   - Save as `couchbase-cloud-cert.pem` in your project directory
6. **Get connection endpoint**:
   - Use the **KV endpoint** (port 11207 for SSL)
   - Format: `your-cluster-id.cloud.couchbase.com:11207`

**Capella SSL Authentication Example**:
```cpp
// Couchbase Capella - SSL is MANDATORY
auto result = couchbase_ops.authenticateSSL(
    "your_username", 
    "your_password", 
    "your-cluster.cloud.couchbase.com:11207",    // SSL port
    "your_bucket_name",                          // bucket name
    "couchbase-cloud-cert.pem"                   // certificate file
);
```

**Important Notes for Capella**:
- **SSL is mandatory** - connections without SSL will fail
- Use port **11207** (SSL) instead of 11210 (non-SSL)
- Certificate verification is required for security
- Ensure firewall allows outbound connections on port 11207

---

### 10. Error Handling Patterns

#### High-Level API (Recommended)
The `CouchbaseOperations` class uses a simple `Result` struct:

```cpp
struct Result {
    bool success;           // true if operation succeeded
    string error_message;   // human-readable error description
    string value;          // returned value (for Get operations)
    uint16_t status_code;   // Couchbase status code (0x00 if success)
};
```

**Recommended Pattern**:
```cpp
auto result = couchbase_ops.add("key", "value");
if (!result.success) {
    LOG(ERROR) << "Add failed: " << result.error_message;
    LOG(ERROR) << "Status code: " << result.status_code;
    // Handle error appropriately
} else {
    std::cout << "Add succeeded!" << std::endl;
}

// For Get operations, check both success and value
auto get_result = couchbase_ops.get("key");
if (get_result.success) {
    std::cout << "Retrieved: " << get_result.value << std::endl;
} else {
    LOG(ERROR) << "Get failed: " << get_result.error_message;
    LOG(ERROR) << "Status code: " << get_result.status_code;
}
```

---
### 11. Best Practices

#### Threading Patterns
> **💡 FLEXIBLE THREADING OPTIONS**
> - **Same bucket/server**: Share a single `CouchbaseOperations` instance across threads
> - **Different buckets**: Create separate instances for each bucket within the same server
> - **Different servers**: Create separate instances for each server connection
> - **Connection isolation**: Each instance uses unique connection groups based on server+bucket combination

#### SSL Security
- **Always use SSL for Couchbase Capella** (cloud deployments)
- **Verify certificates** - don't disable certificate validation in production
- **Use port 11207** for SSL connections
- **Store certificates securely** and update them when they expire

#### Performance
- **Reuse `CouchbaseOperations` instances** - they maintain persistent connections
- **Use pipeline operations for bulk operations** 
- **Pipeline operations preserve order** - results correspond to request order

#### Threading Examples
```cpp
// Option 1: Shared instance for same bucket
brpc::CouchbaseOperations shared_ops;
shared_ops.authenticate(username, password, server_address, bucket_name);

void worker_thread_1() {
    shared_ops.add("key1", "value1");  // Safe to share
}
void worker_thread_2() {
    shared_ops.get("key2");  // Safe to share
}

// Option 2: Separate instances for different buckets
brpc::CouchbaseOperations ops_bucket1;
brpc::CouchbaseOperations ops_bucket2;
ops_bucket1.authenticate(username, password, server_address, "bucket1");
ops_bucket2.authenticate(username, password, server_address, "bucket2");

// Option 3: Separate instances for different servers
brpc::CouchbaseOperations ops_server1;
brpc::CouchbaseOperations ops_server2;
ops_server1.authenticate(username, password, "server1:11210", bucket_name);
ops_server2.authenticate(username, password, "server2:11210", bucket_name);
```

---
### 12. Summary and References
This implementation provides high-level APIs for Couchbase KV operations. Couchbase (the company) contributed to this implementation, but it is not officially supported; it is "[Community Supported](https://docs.couchbase.com/server/current/third-party/integrations.html#support-model)".

---

## 💡 **THREADING USAGE PATTERNS** 💡
> 
> **✅ PATTERN 1: Shared instance when multiple threads operating on the same bucket**
> ```cpp
> brpc::CouchbaseOperations shared_ops;
> shared_ops.authenticate(username, password, "server:11210", "my_bucket");
> 
> void worker_thread_1() {
>     shared_ops.add("key1", "value1");  // ✅ Safe to share
> }
> void worker_thread_2() {
>     shared_ops.get("key2");  // ✅ Safe to share
> }
> ```
> 
> **✅ PATTERN 2: Separate instances when different threads will be operating on different buckets**
> ```cpp
> void worker_thread1() {
>     brpc::CouchbaseOperations ops_bucket1;
>     ops_bucket1.authenticate(username, password, "server:11210", "bucket1");
>     ops_bucket1.add("key1", "value1");
> }
> void worker_thread2() {
>     brpc::CouchbaseOperations ops_bucket2;
>     ops_bucket2.authenticate(username, password, "server:11210", "bucket2");
>     ops_bucket2.add("key1", "value1"); 
> }
> ```
> 
> **✅ PATTERN 3: Separate instances when threads are operating on different servers.**
> ```cpp
> void worker_thread1() {
>     brpc::CouchbaseOperations ops_bucket1;
>     ops_server1.authenticate(username, password, "server1:11210", "bucket1");
>     ops_server1.add("key1", "value1");
> }
> void worker_thread2() {
>     brpc::CouchbaseOperations ops_server2;
>     ops_server2.authenticate(username, password, "server2:11210", "bucket2");
>     ops_server2.add("key1", "value1"); 
> }
> ```
>
> **For additional Couchbase features, consider the couchbase-cxx-SDK version of bRPC, which provides a more complete set of Couchbase features and can be accessed at [Couchbaselabs-cb-brpc](https://github.com/couchbaselabs/cb_brpc/tree/couchbase_sdk_brpc).**


Contributions and issue reports are welcome!
