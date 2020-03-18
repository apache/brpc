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

#ifndef  BRPC_JSON2PB_RAPIDJSON_H
#define  BRPC_JSON2PB_RAPIDJSON_H


#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)

#pragma GCC diagnostic push

#pragma GCC diagnostic ignored "-Wunused-local-typedefs"

#endif

#include "butil/third_party/rapidjson/allocators.h"
#include "butil/third_party/rapidjson/document.h"
#include "butil/third_party/rapidjson/encodedstream.h"
#include "butil/third_party/rapidjson/encodings.h"
#include "butil/third_party/rapidjson/filereadstream.h"
#include "butil/third_party/rapidjson/filewritestream.h"
#include "butil/third_party/rapidjson/prettywriter.h"
#include "butil/third_party/rapidjson/rapidjson.h"
#include "butil/third_party/rapidjson/reader.h"
#include "butil/third_party/rapidjson/stringbuffer.h"
#include "butil/third_party/rapidjson/writer.h"
#include "butil/third_party/rapidjson/optimized_writer.h"

#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)
#pragma GCC diagnostic pop
#endif

#endif  //BRPC_JSON2PB_RAPIDJSON_H
