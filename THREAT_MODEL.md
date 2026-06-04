# Apache bRPC — Threat Model (v1 draft)

## §1 Header

- **Project:** Apache bRPC (`apache/brpc`).
- **Scope:** the `apache/brpc` repository only. The PMC confirmed on 2026-05-20 that `apache/brpc-website` is out of scope for this engagement. This draft is first authored by the ASF Security Team and then reviewed by the PMC.
- **Version binding:** based on `master` around 2026-05-21. Vulnerability reports should be triaged against the model for the corresponding version, not against `HEAD`; re-bind on each release.
- **Authors and status:** drafted by the ASF Security Team (Glasswing pre-scan), revised by PMC member Weibing Wang. **DRAFT v1**
- **Reporting entry point:** at drafting time, the repository had no `SECURITY.md`, and the Apache security project index listed only the generic `security@apache.org` address. Until project-level disclosure documentation is published, report vulnerabilities to `security@apache.org` per ASF policy.
- **Provenance legend:**
  - *(documented — source)*: from repository documentation, headers, gflags, source code, or Apache governance artifacts.
  - *(inferred — Qn)*: inferred from code structure, general RPC security experience, or absence of a defense, with a corresponding question in §14.
  - *(maintainer)*: not yet used in this draft; to be substituted after PMC confirmation.
- **Draft confidence:** most factual descriptions in §1-§13 come from documentation and source code. Intent, default security posture, resource boundaries, the built-in service trust model, and vulnerability triage criteria still require confirmation in §14.
- **bRPC overview:** bRPC is an embeddable C++ RPC framework that supports dispatching multiple protocols on the same port via content sniffing, including `baidu_std`, HTTP/1.x, HTTP/2/gRPC, Thrift, Redis, memcached, RTMP, Mongo, the nshead family, and others. It provides naming services, load balancers, optional TLS, bthread scheduling, and HTTP built-in admin services. bRPC is not a standalone daemon; downstream applications link `libbrpc` and start `brpc::Server` in-process.

---

## §2 Scope and intended use

bRPC's primary uses are:

1. Expose one or more `google::protobuf::Service` implementations on a TCP port, with multiple protocols sharing the same port.
2. Act as a client `brpc::Channel` to access a single server or a cluster described by a naming service plus load balancer.
3. Be embedded by a host application; deployment, TLS, network exposure, authentication, routing, and process lifecycle are the responsibility of the application or operator.
4. Use built-in services such as `/status`, `/vars`, `/flags`, `/connections`, `/rpcz`, and `/health` for debugging and monitoring.
5. Use bRPC as an HTTP/h2 server or client, including Restful URL mapping and JSON-to-Protobuf / Protobuf-to-JSON conversion.

bRPC is not a secure-by-default managed service. Threats land in the runtime code linked by the application and in the built-in admin services the application chooses to expose.

### Caller / actor roles

- **Application developer:** defines `.proto` files, implements services, chooses protocols and ports, and calls clients. Trusted within the compile-time and configuration boundary.
- **Operator:** runs the binary and configures ports, `internal_port`, TLS, gflags, naming services, rate limiting, and built-in services. Trusted for that instance.
- **Client peer (RPC client):** sends bytes to a bRPC server. Untrusted by default; protocol sniffers must handle arbitrary input.
- **Server peer:** the remote server that a bRPC client connects to. Response bytes are also untrusted.
- **Naming-service backend:** `bns`, `file`, `consul`, `nacos`, DNS, and similar backends. Configured by the operator and treated by bRPC as trusted infrastructure.
- **Built-in admin-service consumer:** a person or program accessing `/status`, `/flags`, `/rpcz`, and similar endpoints. Its trust level depends entirely on whether the operator places these endpoints on an internal network or disables them.

### Component-family table

| Family | Representative entry point | Touches outside the process? | In model? |
| --- | --- | --- | --- |
| **C++ runtime core** | `Server::Start`, `Channel::CallMethod`, `Socket`, `InputMessenger` | sockets, files, threads, TLS, optional RDMA | **In model** |
| **Wire-protocol parsers** | `Parse*Message` | reads untrusted bytes from sockets | **In model**, highest priority |
| **HTTP / h2 (+ gRPC) stack** | `HttpServerHandler`, `H2StreamContext`, `URI` | sockets, TLS, json2pb | **In model** |
| **Built-in admin services** | `/status`, `/vars`, `/flags`, `/rpcz`, `/dir`, `/threads`, profiler, metrics | HTTP, `/proc`, filesystem, profiler | **In model**, depending on exposure |
| **TLS / SSL layer** | `ServerSSLOptions`, `ChannelSSLOptions` | OpenSSL, certificate files | **In model** |
| **Authentication hooks** | `Authenticator::VerifyCredential` | wire token | **In model**, policy is implemented by the application |
| **Naming services** | `file://`, `http(s)://`, `consul://`, `nacos://`, `dns://` | files and network | **In model**, backend treated as trusted |
| **Load balancers** | rr, wrr, random, la, consistent hashing | pure computation | **In model**, not a direct attack surface |
| **Compression layer** | gzip/snappy/zlib/lz4 | CPU/memory | **In model** |
| **RDMA transport** | `rdma://` | verbs API, pinned memory | **In model** only when enabled at build time and runtime |
| **Coroutine bridge** | `usercode_in_coroutine` | scheduling | **In model** in non-default mode |
| **bthread / butil / bvar** | scheduler, IOBuf, counters | threads, futex, files, `/proc` | **In model** |
| **mcpack2pb / json2pb** | mcpack/JSON <-> pb | pure computation | **In model** on the corresponding protocol paths |
| **Java / Python bindings** | JNI / pyBRPC | language bridge | currently unsupported; **Out of model** |
| **tools / test / example / build / community / docs** | CLI, tests, examples, build, governance, docs | non-production runtime surface | **Out of model** |

Wire-protocol parsers are the most important surface: they directly receive untrusted bytes, are implemented in C++, cover many protocols with different maturity levels, and multi-protocol sniffing can make every registered parser reachable from the same listening port. `enabled_protocols` can restrict the sniffing set, but its implementation details need maintainer confirmation.

---

## §3 Out of scope (explicit non-goals)

The following scenarios or threats are not protected by the bRPC threat model. Matching reports are closed according to §13.

### Use cases out of scope

1. **A secure-by-default RPC endpoint.** bRPC provides components, not a `brpcd` with a strong default security posture.
2. **A sandbox or process-isolation boundary.** Deserialization, business handlers, and the host application run in the same process.
3. **Cryptographic primitives.** TLS security comes from OpenSSL; bRPC only wraps and configures it.
4. **An application field-validation framework.** bRPC validates wire framing, not semantic fields such as email addresses, IDs, or business ranges.
5. **A complete replacement for authenticated transport.** Bare `baidu_std`, HTTP, Thrift, Redis, and similar protocols have no default authentication; `Authenticator` and mTLS require explicit configuration.
6. **A general HTML/JSON rendering security surface.** Escaping in application HTTP handlers is the application's responsibility.
7. **A general file server.** `/dir` is disabled by default and is not modeled as a production file-serving feature.
8. **Exposing built-in services to the public internet.** bRPC built-in services should only be exposed to trusted internal consumers.

### Threats explicitly out of scope

1. Attackers who control the calling process, can read/write process memory, or can call arbitrary bRPC APIs.
2. Attackers who control the compiler, dependencies, build environment, or supply chain.
3. Side channels such as timing, cache, power, and RowHammer.
4. Raw socket-layer DoS: SYN flood, slowloris, half-open connection exhaustion, and kernel table exhaustion.
5. Resource exhaustion in `bvar`/`bthread`/`butil` caused purely by in-process call patterns.
6. Performance loss caused by an operator intentionally enabling expensive tracing/profiling.
7. Compromise of the naming-service backend; bRPC trusts the backend configured by the operator.
8. Vulnerabilities in application handlers, such as SQL injection or missing business authorization.

### Code shipped in the repo but out of scope

`test/`, `example/`, `tools/`, build and packaging files, `community/`, `docs/`, and `apache/brpc-website` are not modeled as production runtime surfaces. The core boundary is the runtime, parsers, transports, built-in services, and support libraries under `src/`.

---

## §4 Trust boundaries and data flow

bRPC has four main trust boundaries: three on the network surface and one on the operator configuration and naming-service surface.

### Boundary 1 — the wire (server side, business RPC)

Server-side socket bytes are untrusted by default:

```text
socket bytes
  -> Acceptor / Socket
  -> InputMessenger::CutInputMessage
     -> try registered protocol parsers one by one
     -> check whether declared body size exceeds -max_body_size
  -> protocol message structure
  -> protobuf / json2pb / mcpack2pb / other decoder
  -> Service::Method(controller, request, response, done)
```

The trust transition occurs when the framework calls the user's `Service::Method`. Crashes or memory corruption reachable from the wire in parsers, decoders, compression, and TLS handshake are in model. How the business handler processes `Request` is the application's responsibility.

All protocol parsers are enabled by default. `ServerOptions.enabled_protocols` can restrict the protocol sniffing set.

### Boundary 2 — the wire (server side, built-in admin services)

HTTP/h2 requests on the same listening port may be routed to built-in services. `internal_port` can move built-in services to a separate port, and `has_builtin_services=false` can disable them entirely. `/dir` and `/threads` are disabled by default; other built-in services are enabled by default when `has_builtin_services=true`.

Model posture: built-in services are treated as an operator-trusted surface when placed on an internal port or protected by firewall. If exposed to the public internet, they become a major risk surface. Specific triage depends on Q46/Q50 in §14.

Even when built-in services are only served internally, they still need to avoid severe attacks such as command injection or denial of service. However, modifying gflags, enabling rpcz, profiling, and similar operations through built-in services can affect business logic or program performance; those effects are normal use and are not security vulnerabilities.

### Boundary 3 — the wire (client side)

A bRPC client deserializes server responses through paths symmetric to the server side: untrusted bytes enter parsers and decoders before being handed to the application. Parser/decoder vulnerabilities triggered by malicious server responses are in model.

### Boundary 4 — the operator config + naming-service backend

`ServerOptions`, `ChannelOptions`, gflags, flagfiles, TLS file paths, and naming-service URIs are supplied by the operator and are trusted. Server lists returned by naming services are also treated as trusted infrastructure. Backend compromise is out of scope, but crashes in reply parsers on malformed input remain in model.

### Reachability preconditions per family

- Parser issues are in model only when reachable from Boundary 1/3; opt-in protocols require operator enablement.
- Built-in services are triaged according to the Boundary 2 exposure rules.
- TLS issues are inside the network surface; OpenSSL CVEs are handled as external dependency issues.
- Compression, json2pb, and mcpack2pb are in model when reachable from attacker input.
- RDMA and coroutine paths are modeled only when explicitly enabled or loaded.

### Data not crossing a boundary

Purely internal scheduler queues, in-process `bvar` counters, internal `IOBuf` lifetimes, `socket_map` lifetimes, Wireshark dissectors under `tools/`, and similar paths are not trust transitions.

---

## §5 Assumptions about the environment

bRPC is a C++ library that normally runs on Linux/macOS with POSIX sockets, OpenSSL, and a C++ toolchain.

### OS / runtime / hardware

- Linux and macOS are supported; Windows is not included in the security support scope for now.
- A C++11 or newer toolchain is required.
- POSIX sockets and Linux epoll or macOS kqueue are required.
- TLS depends on OpenSSL; risks from old OpenSSL versions are managed by the operator.
- gflags, protobuf, and leveldb are regular dependencies; tcmalloc/gperftools is optional.

### Concurrency

- bthread is an M:N scheduler; the number of worker pthreads depends on CPU count or configuration.
- `ServerOptions.num_threads` is a global worker-count hint, not hard isolation for one server.
- bthread-local state may be reused; user destructors must be reentrant.
- `-usercode_in_pthread` moves user code to pthreads and changes concurrency and queuing semantics.

### Memory model

bRPC is memory-unsafe C++. All `Parse*Message`, `Read*`, `Decompress*`, JSON/mcpack/protobuf decoder paths are candidate surfaces for OOB access, UAF, double-free, integer overflow, and stack overflow. `-max_body_size` is the key outer cap; the model depends on parsers correctly checking lengths and avoiding signed/unsigned overflow.

### Time / clock

bRPC uses `gettimeofday`, `clock_gettime`, `cpuwide_time_us`, and similar clocks. Resistance to timing side channels is not a goal.

### Filesystem / network / peripherals

bRPC opens TCP/Unix sockets, pid files, TLS cert/key files, naming-service files, profile output, rpc dumps, and reads `/proc/*` for bvar. It also sets normal socket options such as keepalive, `SO_REUSEPORT`, and `TCP_USER_TIMEOUT`.

### What bRPC does *not* do to its host (negative-claim inventory)

By default it does not install signal handlers. Normal operation does not fork child processes unless a profiler endpoint is triggered. It reads limited environment variables and gflags. First SSL use initializes OpenSSL global state. Logs go to stdout/stderr or glog. It does not modify locale or FPU state. `fork without exec` must happen before bRPC initialization.

---

## §5a Build-time and configuration variants

bRPC's security boundary is affected by build options, gflags, `ServerOptions`, `ChannelOptions`, and SSL options.

### Build-time options that change the security envelope

| Knob / flag | Default | Effect on model |
| --- | --- | --- |
| `WITH_THRIFT` | off | makes compiling and enabling the Thrift parser possible |
| `WITH_GLOG` | off | changes the logging backend; not critical |
| `WITH_RDMA` | off | introduces RDMA transport and pinned memory |
| `WITH_MESALINK` | off | OpenSSL replacement implementation |
| `BUILD_UNIT_TESTS` | off | builds tests, out of model |

*Note: the table above is only an example, not a complete list. Analyze the latest code for details.*

### Runtime gflags that change the security envelope

| Flag / option | Default | Effect | Maintainer stance |
| --- | --- | --- | --- |
| `-max_body_size` | 64 MiB | global body cap for all protocols; protobuf's internal cap is lifted to `INT_MAX` | critical |
| `-enable_dir_service` | false | `/dir` reads files | dangerous when enabled |
| `-enable_threads_service` | false | `/threads` dumps stacks | dangerous when enabled |
| `-immutable_flags` | false | `/flags` can modify reloadable gflags | safer when enabled |
| `-usercode_in_pthread` | false | changes the thread model for user code | changes concurrency semantics and may cause deadlocks |
| `ServerOptions.has_builtin_services` | true | global switch for built-in services | critical |
| `ServerOptions.internal_port` | -1 | built-in services are on the main port by default | critical; safer when set |

*Note: the table above is only an example, not a complete list. Analyze the latest code for details.*

### Insecure-default summary (the §13 routing question)

Defaults triaged by the PMC:

1. `-max_body_size=64 MiB` is the production default.
2. `has_builtin_services=false` or `internal_port!=-1` is operator responsibility.
3. The combination of `-immutable_flags=false` and reachable `/flags` is acceptable.
4. `ChannelSSLOptions.verify.verify_mode=NOT_SET`, causing client TLS not to verify the server certificate, is acceptable.
5. `ChannelSSLOptions.protocols` including TLS 1.0/1.1 is acceptable.
6. `force_ssl=false`, causing a TLS-configured port to still accept plaintext, is acceptable.
7. `/dir` and `/threads` are disabled by default. If an operator enables them and exposes them publicly, treat that as a dangerous non-default configuration.

*Note: the table above is only an example, not a complete list. Analyze the latest code for details.*

---

## §6 Assumptions about inputs

bRPC receives three classes of input: socket bytes, in-process API parameters, and operator configuration. Boundary 1/3 wire bytes are the highest risk.

### Per-parameter trust table

| Component / function | Parameter | Attacker-controllable? | Caller / operator must enforce |
| --- | --- | --- | --- |
| `InputMessenger::CutInputMessage` | initial connection bytes | yes | sniffer must defend against arbitrary bytes |
| `ParseRpcMessage` | PRPC header, `RpcMeta`, payload | yes | `-max_body_size`, `meta_size <= body_size` |
| `ParseHttpMessage` | method, URI, headers, body | yes | HTTP grammar, body cap |
| `ParseH2Message` | frame, HPACK headers, SETTINGS | yes | frame/header/HPACK/SETTINGS caps |
| Redis / memcache / Thrift / Mongo / nshead parsers, etc. | declared length, frame, body | yes | must be uniformly constrained by `-max_body_size` or protocol caps |
| RTMP / AMF | chunk stream, AMF object | yes | chunk and nesting caps |
| `json2pb::JsonToProtoMessage` | HTTP JSON body | yes | UTF-8, depth, repeated-field expansion |
| `mcpack2pb` | mcpack body | yes | depth and size caps |
| `Decompress` | compressed payload | yes | bRPC currently does not guarantee a decompressed-size cap; decompression may fail |
| TLS handshake | cert, SNI, ALPN | yes | operator configures verify, SNI, ALPN |
| `Authenticator::VerifyCredential` | credential, client_addr | yes; more obvious when header IP is enabled | application implements authentication; operator protects proxy boundary |
| Built-in `/flags` | gflag value | yes if reachable | hide built-ins or set `-immutable_flags=true` |
| Built-in `/dir` | path | yes if enabled | do not enable on public internet |
| Built-in profiler | `seconds` and similar parameters | yes if reachable | hide built-ins and limit profiling cost |
| `ServerOptions` / `ChannelOptions` / gflags | configuration values | no | operator trusted |
| Naming service reply | backend response | trusted by transitivity | parser should still tolerate malformed replies |

### Size, shape, rate

- `-max_body_size=64 MiB` is the global message cap; there is no unified per-method or per-protocol cap.
- The cap for unconsumed streaming RPC bytes is `-socket_max_streams_unconsumed_bytes`; default 0 means unlimited.
- Pending write buffers per socket are limited by `-socket_max_unwritten_bytes`.
- `max_concurrency` limits in-flight requests, not connection count; built-in services are not constrained by this option.
- `idle_timeout_sec` is disabled by default; connection count and slowloris are mainly handled by operators / load balancers.
- Protobuf has a recursion limit; JSON, mcpack, and AMF depth limits need confirmation.
- The framework has no general rate limiting.

---

## §7 Adversary model

### Primary adversary — the wire peer

The primary attacker is the socket peer, which can send arbitrary bytes, confuse inputs across protocols, declare large lengths, construct compression bombs, trigger parser bugs, or make a client parse malicious server responses. Goals include crashes, memory/CPU exhaustion, OOB read/write, framing bypass, arbitrary command execution, authentication bypass, abuse of reachable built-in services, modification of reloadable gflags, and reading information through `/dir`.

### Secondary adversary — the authenticated peer

A peer that passes `Authenticator` is still untrusted at the parser layer. Authentication only narrows the attacker set; it does not change the peer's ability to send bytes.

### Tertiary adversary — the HTTP-built-in-services consumer

When `internal_port < 0` and `has_builtin_services=true`, any HTTP client that can reach the listening port may access `/status`, `/vars`, `/flags`, profiler, metrics, and similar endpoints. Final triage depends on Q46/Q50.

### Out-of-scope adversaries

In-process attackers, local side-channel attackers, attackers controlling `ServerOptions`/`ChannelOptions`, attackers controlling naming-service backends, build-chain attackers, and attacks that depend on the operator failing to harden network boundaries are not objects that bRPC itself promises to defend against.

### Adversary capabilities by transport

- **Plain TCP:** full wire control, no default authentication or encryption.
- **TLS:** provides encryption, but client/server certificate verification and TLS-only behavior require operator configuration.
- **RDMA:** modeled only when enabled; risks come from the verbs API and memory regions.
- **Unix domain socket:** if supported, the trust model is similar to TCP, with socket file permissions managed by the operator.

---

## §8 Security properties the project provides

### Memory and process-safety properties

**P1. Under configured caps, valid wire input should not cause memory corruption.**  
Any OOB access, UAF, double-free, or heap corruption in a parser, decoder, decompressor, TLS path, or built-in handler reachable from Boundary 1/3 is a high/critical issue.

**P2. Bounded recursive structures should not cause stack overflow.**  
Protobuf, JSON, AMF, mcpack, and similar decoders should reject excessively deep input before exhausting the stack.

### Wire-format properties

**P3. Multi-protocol sniffing should be conservative.**  
When a parser returns `TRY_OTHERS`, it should not consume bytes; the same frame should not be taken over by the wrong parser.

**P4. `-max_body_size` should be enforced.**  
Parsers with declared lengths should reject and close the connection when the cap is exceeded; they should not allocate oversized memory first.

### TLS / transport-security properties (when enabled)

**P5. With SSL correctly configured and OpenSSL secure, TLS provides confidentiality and integrity.**

**P6. The server rejects SSLv3 by default.**

**P7. SNI can be used for certificate selection; fallback behavior on no match is controlled by `strict_sni`.**

**P8. ALPN selection should be limited to `ServerSSLOptions.alpns`.**

### Resource-bound properties

**P9. Single-message memory use should be bounded by `-max_body_size`.**  
Linear constant-factor overhead is acceptable; superlinear expansion or allocations that bypass the cap should be treated as bugs. See §9 D5 for decompression expansion.

**P10. Per-socket unwritten buffers should be limited by `-socket_max_unwritten_bytes`.**

**P11. Server/method concurrency caps should take effect when configured by the operator.**

### Concurrency properties

**P12. The framework's own cross-connection state should be thread-safe.** Synchronization of shared state in user handlers is the user's responsibility.

**P13. `Channel::CallMethod` may be called across threads; `Channel::Init()` is not guaranteed to be thread-safe.**

### Built-in admin-service properties (conditional on operator exposure)

**P14. Built-in services should be protected by `has_builtin_services`, `internal_port`, or network boundaries.**

**P15. `/dir` and `/threads` are disabled by default.**

---

## §9 Security properties the project does *not* provide

### Disclaimed properties (downstream's job, not bRPC's)

**D1.** Bare protocols do not have default peer authentication; `Authenticator` authenticates per connection, not per request.  
**D2.** No application-layer authorization is provided; RBAC, ACLs, scopes, and rate limits are implemented by the application or `Interceptor`.  
**D3.** When `-http_header_of_user_ip` is enabled, bRPC does not defend against attacker-forged headers; trusted proxies and firewalls must enforce the boundary.  
**D4.** Without a configured cap, streaming does not defend against unlimited growth of unconsumed stream data.  
**D5.** bRPC does not guarantee a decompressed-output size cap. Decompression failure is allowed, but crashes or memory boundary violations are not.  
**D6.** bRPC does not guarantee a cap on decoded JSON/repeated-field size. Decode failure is allowed, but crashes or memory boundary violations are not.  
**D7.** h2/HPACK bomb, CONTINUATION flood, and PING flood defenses are currently missing and should preferably be added.  
**D8.** No constant-time guarantee is provided.  
**D9.** OpenSSL weaknesses and default TLS version choices are managed by the operator.  
**D10.** No anti-replay is provided; request/sequence IDs are only used for request/response matching.  
**D11.** There is no confidentiality without TLS.  
**D12.** bRPC does not defend against socket-layer connection floods or slowloris.  
**D13.** When built-ins are exposed and `-immutable_flags=false`, bRPC does not defend against `/flags` modifying reloadable gflags.  
**D14.** When built-ins are reachable, bRPC does not defend against information disclosure from `/status`, `/vars`, `/connections`, `/rpcz`, and similar endpoints.  
**D15.** When built-ins are reachable, bRPC does not defend against CPU/IO cost from profiler endpoints.  
**D16.** When `internal_port=-1`, user services and built-in services on the same port are not isolated.  
**D17.** Cross-protocol confusion from the multi-protocol sniffer is a parser issue the framework must defend against, but the risk of not restricting the protocol set is the operator's responsibility.  
**D18.** Pathological protobuf input under protobuf `TotalBytesLimit=INT_MAX` has no additional framework cap.  
**D19.** When `force_ssl=false`, a TLS-configured port still accepts plaintext.

### False-friend properties

**F1.** `has_builtin_services=true` is an administrative surface, not just harmless debug pages.  
**F2.** `Authenticator::VerifyCredential` runs once per connection, not once per request.  
**F3.** `force_ssl=false` means the same port accepts both SSL and non-SSL.  
**F4.** `Controller::remote_side()` may be affected by `-http_header_of_user_ip`.  
**F5.** `verify_depth=0` means verification is disabled, not strict verification.  
**F6.** `Authenticator` does not cover all protocols by default.  
**F7.** `strict_sni=false` means an unknown SNI falls back to the default certificate.  
**F8.** `-max_body_size` does not limit decompressed size, decoded JSON size, or protobuf's internal cap.  
**F9.** The LZ4 enum and documented support status are not fully aligned and need confirmation.

### Well-known attack classes left to the caller

Compression bombs, JSON/repeated-field expansion, HPACK bombs, TLS compression oracles, TLS downgrade, authentication brute force, `/flags` weaponization, slow decoder DoS, HTTP request smuggling, cross-protocol confusion, and naming-service spoofing. Except for parser memory safety and explicit claims, most of these are mitigated by the operator or application.

---

## §10 Downstream responsibilities

### Application developer responsibilities

1. Pin the bRPC version and evaluate against that version's model.
2. Treat all deserialized fields as untrusted application input and perform business-semantic validation.
3. Implement `Authenticator` when identity is required, remembering that it authenticates per connection.
4. Implement `Interceptor` or handler checks when per-request authorization is required.
5. Do not call `Channel::Init()` concurrently across threads; `CallMethod()` is the thread-safe path.
6. Do not cache gflag values long-term when running with reloadable flags.
7. Do not use `assert` for security checks.

### Operator responsibilities

8. Hide built-in services on public ports: set `internal_port`, set `has_builtin_services=false`, or use a firewall.
9. Keep `-enable_dir_service=false` and `-enable_threads_service=false` on public ports.
10. Unless truly needed, set `-immutable_flags=true` or ensure `/flags` is reachable only by trusted operators.
11. Configure TLS: server certificate, client verification, removal of TLS 1.0/1.1, and `force_ssl=true` when needed.
12. Set `max_concurrency`, per-method concurrency, `idle_timeout_sec`, and streaming caps.
13. When enabling `-http_header_of_user_ip`, allow only trusted proxies to access the backend port.
14. Do not run bRPC processes as root.
15. Use escaping when application HTTP handlers output HTML.
16. Put public endpoints behind a load balancer / reverse proxy for connection-rate control, slowloris handling, and L7 hardening.
17. Track OpenSSL patches and rotate TLS material separately.
18. If compression is enabled, limit decompressed size at the application layer or sidecar.
19. Use `enabled_protocols` to expose only required protocols.
20. Choose trusted naming-service backends and protect permissions on flagfiles, cert/key files, and pid files.

### Both

21. Re-read and revise this model on each major bRPC upgrade or whenever a §12 condition occurs.

---

## §11 Known misuse patterns

**M1.** Exposing a server with default `has_builtin_services=true` and `internal_port=-1` to the public internet.  
**M2.** Treating `Authenticator` as per-request authentication.  
**M3.** Enabling `-http_header_of_user_ip` while allowing clients to bypass the trusted proxy and connect directly.  
**M4.** Calling `Channel::Init()` from multiple threads.  
**M5.** Using `assert` for security checks.  
**M6.** Running bRPC as root.  
**M7.** Configuring SSL but forgetting `force_ssl=true`, leaving plaintext available.  
**M8.** Using plaintext protocols across trust boundaries.  
**M9.** Setting `-max_body_size` very large and assuming it will not affect memory.  
**M10.** Assuming compressed output size is limited by the framework.  
**M11.** Enabling `/dir` or `/threads` on the public internet.  
**M12.** Enabling nshead/nova/public/nshead_mcpack and forgetting the sniffer will try these parsers.  
**M13.** Treating sequence/correlation IDs as nonces.  
**M14.** Exposing `/flags` while keeping `-immutable_flags=false`.  
**M15.** Not revalidating `auth_context()` or downstream identity information.  
**M16.** Combining `-usercode_in_pthread` with high concurrency without setting `max_concurrency`.  
**M17.** Using an untrusted naming service while client TLS does not verify the server certificate.  
**M18.** Exposing profiler endpoints to the public internet.  
**M19.** Enabling `/rpcz` on traffic containing sensitive request fields.  
**M20.** Enabling RDMA and exposing it to untrusted peers.

---

## §11a Known non-findings (recurring false positives)

**N1.** A parser reading length and checking it with `FLAGS_max_body_size` is by design; it is an issue only if a parser truly skips the check.  
**N2.** ~~Decompression has no decompressed-size cap, per §9 D5.~~  
**N3.** Built-in services expose internal information; operators should hide them.  
**N4.** `/flags` modifying reloadable gflags without authentication is pending Q46.  
**N5.** `/dir` reads files: disabled by default; enabling it is a dangerous non-default configuration.  
**N6.** `/threads` dumps stacks: disabled by default.  
**N7.** bvar reading `/proc/loadavg` and `/proc/stat` is by design.  
**N8.** Client TLS includes TLS 1.0/1.1 by default; triage pending Q56.  
**N9.** Client TLS does not verify server certificates by default; triage pending Q57.  
**N10.** `force_ssl=false` allowing plaintext is documented behavior.  
**N11.** `Authenticator` running once per connection is documented behavior.  
**N12.** Issues in test/example/tools/build/community are out of model.  
**N13.** Hard-coded certificates under `test/` are out of model.  
**N14.** HTTP Basic credentials in plaintext are a consequence of the operator choosing plaintext transport.  
**N15.** MD5/SHA-1 used for load-balancer hash distribution is not cryptographic use.  
**N16.** Wire-reachable integer overflow/OOB is a valid vulnerability class and should not be mechanically closed.  
**N17.** `session_local_data_factory` lifetime is guaranteed by the user as required by documentation.  
**N18.** The bthread keytable pool retaining objects is a reuse design.  
**N19.** `Channel.Init()` being non-thread-safe is documented behavior.  
**N20.** Runtime certificate add/delete/modify concurrency safety is judged case by case and needs maintainer confirmation.  
**N21.** Built-ins being handled on the main port is default behavior; triage pending Q50.  
**N22.** The sniffer trying multiple parsers on unknown bytes is by design.  
**N23.** A fuzz crash in an opt-in parser can still be `VALID` when the path is enabled.  
**N24.** Protobuf `TotalBytesLimit=INT_MAX` is documented design; the outer layer relies on `-max_body_size`.  
**N25.** Naming-service backend replies are trusted.  
**N26.** Triage for crashes on malformed DNS/consul/nacos replies must distinguish backend compromise from parser hardening.  
**N27.** Brute-force authentication using many short connections is rate-limited by the operator.  
**N28.** `/rpcz` may expose sampled request information; this is a design risk after enabling it.  
**N29.** OpenSSL CVEs are not bRPC CVEs, but they affect deployments.  
**N30.** `fork without exec` after bRPC initialization is unsupported.

---

## §12 Conditions that would change this model

The following changes should trigger model revision:

1. A new wire protocol parser or sniffer registration.
2. A new built-in admin service, especially one with write capability.
3. Changes to security-relevant defaults in §5a: `-max_body_size`, streaming caps, `-immutable_flags`, built-in defaults, `force_ssl`, TLS verification, TLS protocols, and so on.
4. An opt-in protocol becoming enabled by default.
5. Adding a decompressed-size cap.
6. Adding h2/HPACK bomb defenses.
7. Adding a per-request authentication mode.
8. Adding `SECURITY.md` or a project security page to the repository.
9. The PMC publishing a security policy.
10. A new CVE class that cannot be triaged under §13.

Vulnerabilities should be triaged against the model in effect for the affected version, not against the latest `HEAD`.

---

## §13 Triage dispositions

| Disposition | Meaning | Licensed by |
| --- | --- | --- |
| `VALID` | violates a property claimed in §8, with attacker, input, and component all in model | §8, §6, §7 |
| `VALID-HARDENING` | does not violate an existing claim, but the API easily leads to §11 misuse and the project may choose to harden it | §11 |
| `OUT-OF-MODEL: trusted-input` | requires controlling configuration or objects marked trusted by the model | §6 |
| `OUT-OF-MODEL: adversary-not-in-scope` | requires attacker capabilities excluded by the model | §7, §3 |
| `OUT-OF-MODEL: unsupported-component` | located in out-of-scope components such as test/tools/example/build/community/docs | §3 |
| `OUT-OF-MODEL: non-default-build` | appears only under dangerous or non-default configuration | §5a |
| `BY-DESIGN: property-disclaimed` | belongs to a property explicitly disclaimed in §9 | §9 |
| `KNOWN-NON-FINDING` | matches a recurring false-positive pattern in §11a | §11a |
| `MODEL-GAP` | cannot be classified and requires model revision | §12 |

### Worked routing examples

- `Parse*Message` OOB write: `VALID`.
- Private key leak under `test/`: `OUT-OF-MODEL: unsupported-component`.
- Client TLS not verifying server certificates by default: `BY-DESIGN`.
- Remote access to `/flags/max_body_size?setvalue=0`: operator responsibility.
- `/dir?path=/etc/passwd`: if it requires `-enable_dir_service=true`, `OUT-OF-MODEL: non-default-build`.
- A small gzip payload decompressing to huge output and causing OOM: `VALID`.
- h2 HPACK bomb: if defenses are missing and reachable, potentially `VALID`.
- Hostile consul backend redirecting a client: `OUT-OF-MODEL: adversary-not-in-scope`; malformed reply crashes can be judged separately.
- OpenSSL CVE: `BY-DESIGN: property-disclaimed`.
- `Channel.Init()` data race: `BY-DESIGN`, because documentation says it is not thread-safe.

---

## §14 Open questions for the maintainers

Each question includes the draft recommendation and awaits PMC confirmation, correction, or deletion. After confirmation, the corresponding *(inferred)* tags in the body should be updated to *(maintainer)*.

---

### Wave 1 — scope, adversary, and the insecure-default rulings (must answer first)

**Q1.** Is the server-side wire peer untrusted by default, so the runtime cannot assume input matches any registered protocol? Yes.  
**Q2.** Is client-side deserialization of server responses in the same model scope as server-side handling of client requests? Yes.  
**Q40.** Is `-max_body_size=64 MiB` a supported production default, or a baseline that operators must lower per deployment? Production default.  
**Q50.** `has_builtin_services=true` + `internal_port=-1` puts built-ins on the main port by default; how should public exposure of `/flags`/`/version` be triaged? Operator responsibility.  
**Q46.** `-immutable_flags=false` allows reachable `/flags` to modify reloadable gflags; should the default be changed to true? No, operator responsibility.  
**Q57.** Client TLS does not verify server certificates by default; should successful MITM be `BY-DESIGN` or `VALID`/`VALID-HARDENING`? `BY-DESIGN`.  
**Q56.** Is it acceptable that `ChannelSSLOptions.protocols` includes TLS 1.0/1.1 by default? `BY-DESIGN`.  
**Q51.** Is `force_ssl=false`, which makes a TLS port still accept plaintext, only operator responsibility? Operator responsibility.

### Wave 2 — what the runtime does (and does not do) to its host

**Q9.** Is bRPC explicitly an embedded library rather than a secure-by-default daemon? Yes.  
**Q10.** Does the user handler run in the same process as the deserializer, with no sandbox provided by bRPC? Yes.  
**Q11.** Are TLS/SSL cryptographic guarantees entirely inherited from OpenSSL? Yes.  
**Q12.** Does bRPC validate only wire framing, not application field semantics? Yes.  
**Q24.** Is Windows outside the security support environment? Yes.  
**Q28.** Do maintainers agree that parser/read/decompress paths are memory-unsafe C++ attack surfaces? Yes.

### Wave 3 — wire-protocol parser surface (per-protocol hardening parity)

**Q8.** Is the list of default-on, opt-in, and client-only protocols accurate? Yes.  
**Q37.** Should `enabled_protocols` ensure parsers not listed are never tried? Yes.  
**Q22.** Should vulnerabilities in opt-in parsers be triaged as `OUT-OF-MODEL: non-default-build` when not enabled? Parsers are all enabled by default, so triage as `VALID`.  
**Q80.** Do all parsers with length framing uniformly check `FLAGS_max_body_size`? Yes.  
**Q59.** Does h2/HPACK have defenses against bomb, CONTINUATION flood, PING flood, and SETTINGS flood? Preferably yes.  
**Q68.** Does `mcpack2pb` have recursion/depth caps? Preferably yes.  
**Q69.** Does `json2pb` have a JSON depth cap? Preferably yes.

### Wave 4 — built-in admin services trust model

**Q4.** Is the trust status of built-in admin consumers determined entirely by operator exposure? Yes.  
**Q19.** Should public access to default built-ins such as `/version` be triaged as operator responsibility? Yes.  
**Q45.** Because `/dir` and `/threads` are disabled by default, should issues after public enablement be triaged as non-default-build? Yes.  
**Q71.** Do profiler endpoint `seconds` parameters have reasonable upper bounds? How should DoS after exposure be triaged? Preferably yes.  
**Q87.** Is there no isolation between built-in services, so a bug in one handler can affect other services in the same process? This is an issue.

### Wave 5 — TLS / SSL / authentication

**Q52.** Is `strict_sni=false` fallback to the default certificate a supported posture? Yes.  
**Q54.** Is server-side mTLS being disabled by default a supported posture? Yes.  
**Q90.** Which protocols does `Authenticator` actually cover? Does it not cover Thrift/Redis/memcache/Mongo/RTMP/nshead by default? Yes.  
**Q77.** Is an authenticated peer still an untrusted adversary at the parser layer? Yes.  
**Q47.** Does `-http_header_of_user_ip` have no built-in trusted-proxy verification? Yes; this is not an issue.

### Wave 6 — resource bounds, compression, streaming

**Q42.** Should streaming unconsumed bytes being unlimited by default change default behavior or require explicit operator configuration? Yes.  
**Q70.** Is it confirmed that bRPC has no decompressed-output size cap? Should `-max_decompressed_size` be added? Preferably yes.  
**Q73.** Is it true that there is no per-protocol/per-method body cap and only global `-max_body_size`? There is indeed none, and this is acceptable.  
**Q74.** Is there no framework-level connection-count cap? None.  
**Q82.** Can the resource policy be stated as: superlinear memory growth over the wire-declared size is a bug when constrained by `-max_body_size`? Yes.  
**Q93.** Has the HTTP parser been audited for request smuggling defenses? Yes.

### Wave 7 — concurrency, properties, and remaining open items

**Q5.** Does RDMA require explicit build-time and runtime enablement, with related vulnerabilities out of model when not enabled? Yes.  
**Q7.** Are `test/`, `example/`, `tools/`, and `community/` out of model? Yes.  
**Q18.** Do wire bytes remain untrusted until the user's `Service::Method` is called? Yes.  
**Q20.** Is client response parsing modeled equivalently to server request parsing? Yes.  
**Q23.** Can crashes on malformed naming-service replies be `VALID`, while malicious backend redirection is out of model? Yes.  
**Q72.** Should consul/nacos and similar naming-service parsers defend against malformed input? Yes.  
**Q79.** Does bRPC have continuous fuzzing/OSS-Fuzz; if not, does P1 mainly rely on code review and CI? No.  
**Q83.** Under the per-connection bthread model, is synchronization of application shared state entirely the user's responsibility? Yes.  
**Q84.** Is per-request authorization implemented by `Interceptor` or the handler, not `Authenticator`? Yes.  
**Q88.** Should cross-protocol sniffer confusion be handled as a framework `VALID` issue? Yes.

---

## §15 Optional: machine-readable companion

v1 does not generate a machine-readable sidecar. After the defaults and triage decisions in §14 wave 1 stabilize, generate a derived index for tooling: entry points and parameter trust levels, in-scope/out-of-scope components, security-relevant gflag defaults, §8 properties, §9 disclaimed properties, §11a non-findings, and §13 disposition labels. This document remains the normative specification.

---

## Appendix: SECURITY.md statement -> threat-model § back-map

At drafting time, the repository had no `SECURITY.md`, and the Apache security project index provided only the generic `security@apache.org` address, with no bRPC project-level security page.

| Source | Statement | Threat-model section |
| --- | --- | --- |
| `security.apache.org/projects/` | no project-level security content | N/A |
| future `SECURITY.md` | TBD by PMC | to be backfilled in a future version |

Once the PMC publishes `SECURITY.md` or an official security policy, that artifact should become a higher-authority source. This document should then map back to or link to the official document.

---

*End of v1 draft.*

