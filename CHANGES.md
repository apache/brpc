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

- [0.9.8-rc01](#0.9.8-rc01)

## 0.9.8-rc01

* Fix bug that time unit is not listed in grpc timeout options.
https://github.com/apache/incubator-brpc/pull/1036
* Fix heap overflow in simple_data_pool
https://github.com/apache/incubator-brpc/pull/1056
* Make args of redis protocol be StringPiece
https://github.com/apache/incubator-brpc/pull/1128
* Support the length of redis args could be zero
https://github.com/apache/incubator-brpc/pull/1128
* Optimize code in timer_thread
https://github.com/apache/incubator-brpc/pull/1137/files
* Fix a narrowing warning on aarch64
https://github.com/apache/incubator-brpc/commit/87f149c464ea0322a5b59d040bb80e7847f365be
* Make pthread_atfork not registered in child process
https://github.com/apache/incubator-brpc/commit/2f8fc37d52c2a02ee6f348aaa52c7ded4a4844c3
* Rename LOG_NONE which conflicts with a name in mysql
https://github.com/apache/incubator-brpc/commit/e3840c18cdd9e1ff81f8280b96ac14869007a122
* Fix several warnings under MAC
https://github.com/apache/incubator-brpc/commit/f8c188a7a5186c2d43a20735ad175a32b39788a3
* Ignore ELIMIT for circuit breaker
https://github.com/apache/incubator-brpc/pull/1005
* limit minimum value of max_concurrency for auto_cl
https://github.com/apache/incubator-brpc/pull/1003
