<!--
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
-->

# Table of Contents

- [0.9.7](#0.9.7)
- [0.9.6](#0.9.6)
- [0.9.5](#0.9.5)
- [0.9.0](#0.9.0)

## 0.9.7
* Add DISCLAIMER-WIP as license issues are not all resolved
* Fix many license related issues
* Ignore flow control in h2 when sending first request
* Add flame graph view for profiling builtin service
* Fix bug that _avg_latency maybe zero in lalb
* Fix bug that logging namespace conflicts with others
* Add gdb_bthread_stack.py to read bthread stack
* Adapt to Arm64
* Support redis server protocol
* Enable circuit breaker for backup request
* Support zone for bilibili discovery naming service when fetching server nodes
* Add brpc revision in release version

## 0.9.6
* Health (of a connection) can be checked at rpc-level
* Fix SSL-related compilation issues on Mac
* Support SSL-replacement lib MesaLink
* Support consistent hashing with ketama algo.
* bvar variables can be exported for prometheus services
* String[Multi]Splitter supports '\0' as separator
* Support for bilibili discovery service
* Improved CircuitBreaker
* grpc impl. supports timeout

## 0.9.5
* h2c/grpc are supported now, h2(encrypted) is not included.
* thrift support.
* Mac build support
* Extend ConcurrencyLimiter to control max-concurrency dynamically and an "auto" CL is supported by default
* CircuitBreaker support to isolate abnormal servers more effectively

## 0.9.0
* Contains major features of brpc, OK for production usages.
* No h2/h2c/rdma support, Mac/Windows ports are not ready yet.
