## Couchbase bRPC Binary Protocol Integration

This document explains the implementation of Couchbase Binary Protocol support added to this branch of bRPC, the available request/response operations, collection support, and how to run the provided example client against either a local Couchbase Server cluster or a Couchbase Capella (cloud) deployment.

---
### 1. Overview

The integration adds a new protocol handler (`PROTOCOL_COUCHBASE`) that allows using bRPC's asynchronous / pipelined request machinery to talk directly to Couchbase Server using its Couchbase Binary Protocol.

The core pieces are:
* `src/brpc/policy/couchbase_protocol.[h|cpp]` – framing + parse loop for binary responses, and request serialization pass‑through.
* `src/brpc/couchbase.[h|cpp]` – high level request/response builders (`CouchbaseRequest`), parsers (`CouchbaseResponse`) and error-handlers.
* `example/couchbase_c++/couchbase_client.cpp` – an end‑to‑end example performing authentication, bucket selection, CRUD operations, pipelining, and collection‑scoped operations.

Design goals:
* Keep wire structs identical to the binary protocol (24‑byte header, network order numeric fields).
* Allow batching multiple operations in one TCP write using bRPC's pipelining.
* Provide ergonomic helpers (Add, Get, Upsert, Delete, Increment/Decrement, SelectBucket, GetCollectionId, etc.).
* Future extensions.

---
### 2. Features

| Category | Supported Operations | Notes |
|----------|----------------------|-------|
| Authentication | SASL `PLAIN` (`CB_BINARY_SASL_AUTH`) | Sent automatically when you enqueue an `Authenticate` request before others. |
| Bucket selection | `SelectBucket` (`CB_SELECT_BUCKET`) | Required before document operations (unless default bucket context). |
| Basic KV | Add / Set(Upsert) / Delete / Get | Flags + Exptime handled. CAS values returned. |
| Pipelining | Yes | Multiple independent binary requests in one buffer, responses drained in order. |
| Collections | `GetCollectionId`, collection‑scoped CRUD (key + collection id) | Parsing currently truncates to 8 bits in public API (upgrade path below). |
| Error Handling | Status → string mapping + formatted error message | Unsupported codes produce generic fallback. |

Missing / Future candidates: Sub‑Document operations, Durability requirements, DCP, Collections Manifest retrieval, Extended Attributes (XATTR), Hello feature negotiation, TLS bootstrap.

---
### 3. Binary Protocol Mapping

Couchbase binary protcol header
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
### 4. Request Building (`CouchbaseRequest`)

High‑level helpers append one or more wire messages onto an internal `IOBuf`. After you schedule all operations, bRPC sends the accumulated buffer over the channel.

Examples:
```cpp
CouchbaseRequest req;
req.Authenticate(user, pass);       // SASL PLAIN
req.SelectBucket("travel-sample");
req.Add("doc::1", json_body, flags, exptime, /*cas*/0);
req.Get("doc::1");                 // Pipeline GET after ADD

channel.CallMethod(nullptr, &cntl, &req, &resp, nullptr);
```

Pipelining count (`pipelined_count()`) is tracked so the parser knows when the full response group is collected.

Collection ID retrieval:
```cpp
CouchbaseRequest coll_req;
coll_req.GetCollectionId("_default", "my_collection");
```
This builds a key of the form `scope.collection` with opcode `0xbb`.

Collection‑scoped CRUD reuse existing helpers with the final `coll_id` byte parameter.

---
### 5. Response Parsing (`CouchbaseResponse`)

Each `Pop*` method consumes the front of the internal response buffer, validating:
1. Header present.
2. Opcode matches expected operation.
3. Status == success (otherwise `_err` filled with formatted message).
4. Body length sufficient.

Common patterns:
```cpp
uint64_t cas;
if (resp.PopAdd(&cas)) { /* success */ } else { LOG(ERROR) << resp.LastError(); }

std::string val; uint32_t flags; uint64_t get_cas;
if (resp.PopGet(&val, &flags, &get_cas)) { /* use val */ }
```

Collection ID parsing (current state):
* Reads 8‑byte Manifest UID + 4‑byte Collection ID extras.
* Truncates Collection ID to `uint8_t` for API compatibility.

The raw response buffer can be inspected via `raw_buffer()` for debug / future operations.

---
### 6. Example Client Walkthrough

`example/couchbase_c++/couchbase_client.cpp` flow:
1. Build channel with `PROTOCOL_COUCHBASE`.
2. Prompt for username/password (SASL PLAIN auth).
3. Select bucket.
4. Perform Add, duplicate Add (expected failure), Get.
5. Add several documents (pipelined) and read responses sequentially.
6. Mixed pipelined GET operations (existing + missing keys) to showcase error handling.
7. Upsert existing and new documents; verify with subsequent Get.
8. Delete existing and missing keys.
9. Retrieve Collection ID for a target collection, then perform collection‑scoped Add/Get/Upsert/Get/Delete if ID retrieved.

Removed instrumentation: The example originally timed operations; those statements were stripped per request to keep output concise.

---
### 7. Building and Running the Example

Build (Make):
```bash
cd example/couchbase_c++/
make
./couchbase_client
```

You will be prompted for:
```
Enter Couchbase username: Administrator
Enter Couchbase password: ********
Enter Couchbase bucket name: travel-sample
```

Ensure buckets/collections you test exist (or create them via UI/CLI) before collection‑scoped CRUD.

---
### 8. Setting Up Couchbase

#### A. Local Install (Non‑Docker)
Download from: https://www.couchbase.com/downloads/ (Community or Enterprise). Install and repeat the same initialization steps through the Web Console at `http://localhost:8091`.
- Open http://localhost:8091 in a browser and follow setup wizard:
- Set admin credentials (Administrator / password)
- Accept terms, choose services (Data, Query, Index at minimum)
- Initialize cluster
- Create a bucket (e.g. travel-sample or custom)

Create a collection (7.0+):

- In the Web Console navigate: Buckets → Your Bucket → Scopes & Collections.
- Add a Scope (optional) or use `_default`.
- Add a Collection (e.g. `testing_collection`).

#### B. Couchbase Capella (Cloud)
1. Sign up / log in: https://cloud.couchbase.com/
2. Create a Free Trial or a Hosted Cluster.
3. Create a bucket (or load a sample dataset).
4. Create a database access credential (API key or user with appropriate RBAC roles – Data Reader/Writer, Bucket Admin as needed).
5. Get the connection string (choose the internal or public endpoint).
6. Update `--server` flag (or code) to point to the KV endpoint host:port.

---
### 9. Error Handling Patterns

Each `Pop*` method returns `false` on:
* Mismatched opcode
* Incomplete buffer
* Non‑zero status (error) – `_err` stores a human readable string like: `STATUS_KEY_EEXISTS: Add operation failed: Key already exists`

Recommended usage:
```cpp
uint64_t cas;
if (!resp.PopAdd(&cas)) {
		LOG(ERROR) << resp.LastError();
}
```

For pipelined batches, call the matching `Pop*` in the same sequence you enqueued the operations.

---
### 10. Summary
This implementation provides a foundation for high‑performance Couchbase KV and collection operations through bRPC's pipelined framework. Extending to sub‑doc, durability, TLS, and richer collection metadata are natural next steps. Contributions and issue reports are welcome.