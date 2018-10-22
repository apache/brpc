// Copyright (c) 2015 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)

#ifndef BRPC_RPCZ_H
#define BRPC_RPCZ_H

#include <string>
#include <ostream>
#include "brpc/span.pb.h"
#include "brpc/span_exporter.h"

namespace brpc {

class SpanFilter {
public:
    virtual bool Keep(const BriefSpan&) = 0;
};

int RegisterRpczSpanExporterOnce();

// Find a span by its trace_id and span_id, serialize it into `span'.
int FindSpan(uint64_t trace_id, uint64_t span_id, RpczSpan* span);

// Find spans by their trace_id, serialize them into `out'
void FindSpans(uint64_t trace_id, std::deque<RpczSpan>* out);

// Put at most `max_scan' spans before `before_this_time' into `out'.
// If filter is not NULL, only push spans that make SpanFilter::Keep()
// true.
void ListSpans(int64_t before_this_time, size_t max_scan,
               std::deque<BriefSpan>* out, SpanFilter* filter);

void TracingSpan2RpczSpan(const TracingSpan* in, RpczSpan* out);

void TracingSpan2BriefSpan(const TracingSpan* in, BriefSpan* out);

void DescribeSpanDB(std::ostream& os);

// Extract name and annotations from Span::info()
class SpanInfoExtractor {
public:
    SpanInfoExtractor(const char* info);
    bool PopAnnotation(int64_t before_this_time,
                       int64_t* time, std::string* annotation);
private:
    butil::StringSplitter _sp;
};

} // namespace brpc


#endif // BRPC_RPCZ_H
