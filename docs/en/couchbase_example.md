## Couchbase bRPC Binary Protocol Integration

This document explains the implementation of Couchbase Binary Protocol support added to this branch of bRPC, the available high-level operations, collection support, SSL authentication, and how to run the provided example client against either a local Couchbase Server cluster or a Couchbase Capella (cloud) deployment. However, the couchbase binary protocol implementation in bRPC currently do not have fine-grained optimizations which has been already done in the couchbase-cxx-client SDK which also have query support, better error handling and much more optimized operations. So, we also added the support of couchbase using couchbase-cxx-SDK in bRPC and is available at [Couchbaselabs-cb-brpc](https://github.com/couchbaselabs/cb_brpc/tree/couchbase_sdk_brpc).

---
### 1. Overview

The integration provides high-level APIs for communicating with Couchbase Server using its Binary Protocol, using the high-level `CouchbaseOperations` class which provides a simplified interface.

> Each thread must create and use its own `CouchbaseOperations` instance. Sharing instances will cause race conditions and unpredictable behavior.

The core pieces are:
* `src/brpc/policy/couchbase_protocol.[h|cpp]` – framing + parse loop for binary responses, and request serialization.
* `src/brpc/couchbase.[h|cpp]` – high-level `CouchbaseOperations` class with simple methods, plus low-level request/response builders (`CouchbaseRequest`), parsers (`CouchbaseResponse`) and error-handlers.
* `example/couchbase_c++/couchbase_client.cpp` – an end‑to‑end example using the high-level API for authentication, bucket selection, CRUD operations, and collection‑scoped operations.
* `example/couchbase_c++/multithreaded_couchbase_client.cpp` – a multithreaded example showing how each thread creates its own `CouchbaseOperations` instance.

Design goals:
* **SSL Support**: Built-in SSL/TLS support for secure connections to Couchbase Capella.
* **Per-instance Authentication**: Each `CouchbaseOperations` object maintains its own authenticated session.
* **Collection Support**: Native support for collection-scoped operations.
* Keep wire structs identical to the binary protocol (24‑byte header, network order numeric fields).
* Future extensions for advanced features.

---
### 2. Features

| Category | Supported Operations | Notes |
|----------|----------------------|-------|
| **High-Level API** | `CouchbaseOperations` class | **Recommended**: Simple methods returning `Result` structs |
| **SSL/TLS Support** | Built-in SSL encryption | **Required** for Couchbase Capella, optional for local clusters |
| Authentication | SASL `PLAIN` with SSL | Each `CouchbaseOperations` instance requires authentication |
| Bucket selection | `selectBucket()` method | Required before document operations |
| Basic KV | `add()`, `upsert()`, `delete_()`, `get()` | Clean API with `Result` struct error handling |
| **Pipeline Operations** | `beginPipeline()`, `pipelineRequest()`, `executePipeline()` | **NEW**: Batch multiple operations in single network call for improved performance |
| Collections | Collection-scoped CRUD operations | Pass collection name as optional parameter (defaults to "_default") |
| Error Handling | `Result.success` + `Result.error_message` | Human-readable error messages with status codes |

- **Simplified**: No need to manage channels, controllers, or response parsing
- **Thread-Safe Per Instance**: Each `CouchbaseOperations` instance can be used independently ⚠️ **BUT NEVER SHARE BETWEEN THREADS**
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

**Approach**: Use the `CouchbaseOperations` class for operations. It shall be noted that the instances of `CouchbaseOperations` should not be shared between threads, for some cases it might work but not recommended.

#### Basic Usage:
```cpp
#include <brpc/couchbase.h>

brpc::CouchbaseOperations couchbase_ops;

// 1. Authenticate (REQUIRED for each instance)
brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticate(
    username, password, server_address, enable_ssl, cert_path);
if (!auth_result.success) {
    LOG(ERROR) << "Auth failed: " << auth_result.error_message;
    return -1;
}

// 2. Select bucket (REQUIRED)
brpc::CouchbaseOperations::Result bucket_result = couchbase_ops.selectBucket("my_bucket");
if (!bucket_result.success) {
    LOG(ERROR) << "Bucket selection failed: " << bucket_result.error_message;
    return -1;
}

// 3. Perform operations
brpc::CouchbaseOperations::Result add_result = couchbase_ops.add("user::123", json_value);
if (add_result.success) {
    std::cout << "Document added successfully!" << std::endl;
} else {
    std::cout << "Add failed: " << add_result.error_message << std::endl;
}
```

#### SSL Authentication (Essential for Couchbase Capella):
```cpp
// For Couchbase Capella (cloud) - SSL is REQUIRED
brpc::CouchbaseOperations::Result auth_result = couchbase_ops.authenticate(
    username, 
    password, 
    "cluster.cloud.couchbase.com:11207",  // SSL port
    true,                                   // enable_ssl = true
    "path/to/certificate.pem"             // certificate path
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

**Pipeline Management Methods**:
- `beginPipeline()` - Start a new pipeline session
- `pipelineRequest(op_type, key, value, collection)` - Add operation to pipeline
- `executePipeline()` - Execute all operations and return results
- `clearPipeline()` - Clear pipeline without executing (cleanup)
- `isPipelineActive()` - Check if pipeline is active
- `getPipelineSize()` - Get number of queued operations

**Performance Benefits**:
- **Reduced Network Overhead**: Multiple operations in single network round-trip
- **Better Throughput**: Especially beneficial for bulk operations
- **Preserved Order**: Results returned in same order as requests
- **Error Isolation**: Individual operation failures don't affect others

#### Error Handling Pattern:
```cpp
brpc::CouchbaseOperations::Result result = couchbase_ops.someOperation(...);
if (!result.success) {
    // Handle error
    LOG(ERROR) << "Operation failed: " << result.error_message;
    LOG(ERROR) << "Error Code: "<< result.status_code;  //what is the error code received.
} else {
    // Use result.value if applicable (for Get operations)
    std::cout << "Retrieved value: " << result.value << std::endl;
}
```

---
### 5. Request/Response Class (`CouchbaseRequest`/`CouchbaseResponse`)

These classses are private to the `CouchbaseOpeartions` and is not exposed to the user. These classes are responsible for building the request that needs to be sent and received over the channel. A basic overview of how the request/response classes works internally has been shown below.

#### Request Building:
```cpp
CouchbaseRequest req;
req.Authenticate(user, pass);       // SASL PLAIN
req.selectBucketRequest("travel-sample");
req.addRequest("doc::1", json_body, flags, exptime, /*cas*/0);
req.Get("doc::1");                 // Pipeline GET after ADD

channel.CallMethod(nullptr, &cntl, &req, &resp, nullptr);
```

#### Response Parsing:
Each `Pop*` method consumes the front of the internal response buffer, validating:
1. Header present.
2. Opcode matches expected operation.
3. Status == success (otherwise `_err` filled with formatted message).
4. Body length sufficient.

---
### 6. Example Client Walkthrough

#### Single-Threaded Example (`couchbase_client.cpp`)
Uses the **high-level `CouchbaseOperations` API**:

1. **Create `CouchbaseOperations` instance** - can create more than one per thread.
2. **Prompt for credentials** - username/password for authentication.
3. **SSL Authentication** - with support for Couchbase Capella certificate-based SSL.
4. **Select bucket** - required before any document operations.
5. **Basic CRUD operations**:
   - Add document (should succeed)
   - Try adding same key again (should fail with "key exists")
   - Get document (retrieve the added document)
6. **Multiple document operations** - Add several documents with different keys.
7. **Upsert operations**:
   - Upsert existing document (should update)
   - Upsert new document (should create)
   - Verify with Get operations
8. **Delete operations**:
   - Delete non-existent key (should fail gracefully)
   - Delete existing key (should succeed)
9. **Collection-scoped operations** - Add/Get/Upsert/Delete in specific collections.
10. **Pipeline operations demo**:
    - Begin pipeline and add multiple operations
    - Execute batch operations in single network call
    - Process results in order
    - Collection-scoped pipeline operations
    - Error handling and cleanup

#### Multithreaded Example (`multithreaded_couchbase_client.cpp`)
Demonstrates:
- **16 bthreads** (4 threads per bucket across 4 buckets)
- **Each thread creates its own `CouchbaseOperations` instance**
- **Independent authentication** per thread
- **Concurrent operations** across multiple buckets and collections

Key difference from single-threaded: Each thread must authenticate independently.

---
### 7. Building and Running the Examples

#### Build both examples:
```bash
cd example/couchbase_c++/
make
```

#### Run Single-Threaded Example:
```bash
./couchbase_client
```

#### Run Multithreaded Example:
```bash
./multithreaded_couchbase_client --operations_per_thread=20 --sleep_ms=100
```

#### Interactive Prompts:
Both examples will prompt for:
```
Enter Couchbase username: your_username
Enter Couchbase password: ********
Enter Couchbase bucket name: your_bucket
```

For multithreaded example, additional prompts:
```
Enter 4 bucket names:
Bucket 1: bucket1
Bucket 2: bucket2
Bucket 3: bucket3  
Bucket 4: bucket4
Number of collections (0 for none): 2
Collection 1: collection1
Collection 2: collection2
```

#### SSL Configuration:
- **Local Couchbase**: SSL is optional, set `enable_ssl = false`
- **Couchbase Capella**: SSL is **required**, download the certificate from Capella console
- Update the server address in the code or use command line flags:
  ```bash
  ./couchbase_client --server="your-cluster.cloud.couchbase.com:11207"
  ```

Ensure buckets/collections exist before testing collection‑scoped operations.

---
### 8. Setting Up Couchbase

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
// Local without SSL
auto result = couchbase_ops.authenticate(username, password, "localhost:11210", false, "");
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
auto result = couchbase_ops.authenticate(
    "your_username", 
    "your_password", 
    "your-cluster.cloud.couchbase.com:11207",    // SSL port
    true,                                        // enable_ssl = true
    "couchbase-cloud-cert.pem"                   // certificate file
);
```

**Important Notes for Capella**:
- **SSL is mandatory** - connections without SSL will fail
- Use port **11207** (SSL) instead of 11210 (non-SSL)
- Certificate verification is required for security
- Ensure firewall allows outbound connections on port 11207

---
### 9. Pipeline Operations (Performance Optimization)

Pipeline operations allow you to batch multiple Couchbase operations into a single network call, significantly improving performance for bulk operations.

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

#### Basic Pipeline Example

```cpp
#include <brpc/couchbase.h>

brpc::CouchbaseOperations couchbase_ops;
// ... authenticate and select bucket ...

// 1. Begin pipeline
if (!couchbase_ops.beginPipeline()) {
    LOG(ERROR) << "Failed to begin pipeline";
    return -1;
}

// 2. Add operations to pipeline (not executed yet)
bool success = true;
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "user1", "{\"name\":\"John\"}");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "user2", "{\"name\":\"Jane\"}");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "user1");
success &= couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::UPSERT, "user3", "{\"name\":\"Bob\"}");

if (!success) {
    couchbase_ops.clearPipeline();
    return -1;
}

// 3. Execute all operations in single network call
std::vector<brpc::CouchbaseOperations::Result> results = couchbase_ops.executePipeline();

// 4. Process results (same order as requests)
for (size_t i = 0; i < results.size(); ++i) {
    const auto& result = results[i];
    if (result.success) {
        std::cout << "Operation " << i << " succeeded";
        if (!result.value.empty()) {
            std::cout << " - Value: " << result.value;
        }
        std::cout << std::endl;
    } else {
        std::cout << "Operation " << i << " failed: " << result.error_message << std::endl;
    }
}
```

#### Collection-Scoped Pipeline Operations

```cpp
// Pipeline with collection operations
couchbase_ops.beginPipeline();

// Add operations to specific collection
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "doc1", "value1", "my_collection");
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "doc1", "", "my_collection");
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::DELETE, "doc2", "", "my_collection");

auto results = couchbase_ops.executePipeline();
// Process results...
```

#### Pipeline Error Handling

Pipeline operations provide granular error handling - each operation can succeed or fail independently:

```cpp
couchbase_ops.beginPipeline();

// Some operations may succeed, others may fail
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::ADD, "existing_key", "value");  // May fail if key exists
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::GET, "nonexistent_key");        // May fail if key doesn't exist
couchbase_ops.pipelineRequest(brpc::CouchbaseOperations::UPSERT, "new_key", "value");    // Should succeed

auto results = couchbase_ops.executePipeline();

// Check each result individually
for (size_t i = 0; i < results.size(); ++i) {
    if (!results[i].success) {
        LOG(WARNING) << "Operation " << i << " failed: " << results[i].error_message;
        // Handle individual failures as needed
    }
}
```

#### Performance Benefits

- **Reduced Network Latency**: Multiple operations in single round-trip
- **Better Throughput**: Especially beneficial for bulk operations

#### Pipeline Best Practices

1. **Batch Related Operations**: Group logically related operations together
2. **Handle Partial Failures**: Individual operations can fail while others succeed
3. **Clear on Errors**: Use `clearPipeline()` if pipeline setup fails
5. **Mixed Operations**: Combine different operation types (GET, ADD, UPSERT, DELETE) as needed

#### When to Use Pipelines

**Ideal Use Cases**:
- Bulk data loading/migration
- Batch processing workflows
- Multi-document transactions (where order matters)
- Performance-critical applications with multiple operations

**Not Recommended For**:
- Single operations (use regular methods)
- Operations requiring immediate results for decision making
- Very large batches that might timeout

---
### 10. Error Handling Patterns

#### High-Level API (Recommended)
The `CouchbaseOperations` class uses a simple `Result` struct:

```cpp
struct Result {
    bool success;           // true if operation succeeded
    string error_message;   // human-readable error description
    string value;          // returned value (for Get operations)
};
```

**Recommended Pattern**:
```cpp
auto result = couchbase_ops.add("key", "value");
if (!result.success) {
    LOG(ERROR) << "Add failed: " << result.error_message;
    // Handle error appropriately
} else {
    std::cout << "Add succeeded!" << std::endl;
}

// For Get operations, check both success and value
auto get_result = couchbase_ops.Get("key");
if (get_result.success) {
    std::cout << "Retrieved: " << get_result.value << std::endl;
} else {
    LOG(ERROR) << "Get failed: " << get_result.error_message;
}
```

---
### 11. Best Practices

#### Thread Safety
> ⚠️ **: THREAD SAFETY REQUIREMENTS**
> - **Each thread MUST create its own `CouchbaseOperations` instance**
> - **Each instance MUST authenticate independently**  
> - **NEVER share `CouchbaseOperations` objects between threads**
> - **Sharing instances will cause race conditions, data corruption, and crashes**

#### SSL Security
- **Always use SSL for Couchbase Capella** (cloud deployments)
- **Verify certificates** - don't disable certificate validation in production
- **Use port 11207** for SSL connections
- **Store certificates securely** and update them when they expire

#### Performance
- **Reuse `CouchbaseOperations` instances** - they maintain persistent connections
- **Use pipeline operations for bulk operations** 
- **Pipeline operations preserve order** - results correspond to request order

#### Code Example Template
```cpp
#include <brpc/couchbase.h>

int main() {
    brpc::CouchbaseOperations couchbase_ops;
    
    // Authenticate (adjust SSL settings as needed)
    auto auth_result = couchbase_ops.Authenticate(
        username, password, server_address, enable_ssl, cert_path);
    if (!auth_result.success) {
        LOG(ERROR) << "Authentication failed: " << auth_result.error_message;
        return -1;
    }
    
    // Select bucket
    auto bucket_result = couchbase_ops.selectBucket(bucket_name);
    if (!bucket_result.success) {
        LOG(ERROR) << "Bucket selection failed: " << bucket_result.error_message;
        return -1;
    }
    
    // Perform operations with error handling
    auto result = couchbase_ops.add("key", "value", "collection_name");
    if (result.success) {
        std::cout << "Success!" << std::endl;
    } else {
        LOG(ERROR) << "Operation failed: " << result.error_message;
    }
    
    return 0;
}
```

---
### 12. Summary and References
This implementation provides high-level APIs for Couchbase KV and collection operations. Couchbase (the company) contributed to this implementation, but it is not officially supported; it is "[Community Supported](https://docs.couchbase.com/server/current/third-party/integrations.html#support-model)".

- **High-level API**: Recommended for most applications - simple, with built-in SSL support
- **SSL Support**: Essential for Couchbase Capella and secure local deployments
- **Thread Safety**: Each thread should create its own authenticated `CouchbaseOperations` instance
- **Collection Support**: Native support for collection-scoped operations


---

## ⚠️ **CRITICAL THREAD SAFETY WARNING** ⚠️

> **🚨 NEVER SHARE `CouchbaseOperations` INSTANCES BETWEEN THREADS! 🚨**
> 
> **Each thread MUST create its own `CouchbaseOperations` instance.**
>
>**Each thread can have multiple `CouchbaseOperations` instances.**
> 
> **For thread safe design please use couchbase-cxx-SDK version of bRPC, While it does not leverage many of the bRPC features around memory management and IO, it does provide a more complete set of Couchbase features and may be useful to those who have apps using bRPC with either memcached binprot or Couchbase and need some of the additional services and can be accessed at [Couchbaselabs-cb-brpc](https://github.com/couchbaselabs/cb_brpc/tree/couchbase_sdk_brpc).**
>
> **✅ CORRECT:**
> ```cpp
> // Each thread creates its own instance
> void worker_thread() {
>     brpc::CouchbaseOperations ops;  // ✅ Thread-local instance
>     ops.authenticate(...);
>     ops.get("key");  // Safe
> }
> ```
> 
> **❌ WRONG - WILL CAUSE CRASHES:**
> ```cpp
> brpc::CouchbaseOperations global_ops;  // ❌ Shared instance
> void worker_thread() {
>     global_ops.get("key");  // ❌ RACE CONDITION - WILL CRASH!
> }
> ```
> 
> **Why?** `CouchbaseOperations` contains mutable state (pipeline queues, buffers, connection state) that is NOT thread-safe. Sharing instances will cause data corruption, pipeline interference, and application crashes.

Contributions and issue reports are welcome!
