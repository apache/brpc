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

// Authors: Lei He(helei@qiyi.com)

#ifndef BRPC_SPAN_EXPORTER_H
#define BRPC_SPAN_EXPORTER_H

#include <memory>
#include <ostream>
#include <set>
#include "butil/memory/singleton_on_pthread_once.h"
#include "butil/synchronization/lock.h"
#include "brpc/span.pb.h"

namespace brpc {

// You can customize the way the span is exported by inheriting SpanExporter.
// Note:
// 1. If you call RegisterSpanExporter multiple times for the same SpanExporter
// object, the object's DumpSpan() method will also be executed multiple times.
// 2. The order in which each SpanExporter::DumpSpan is called is independent 
// of the registration order.
// 3. SpanDumper object should be managered by std::shared_ptr.
//
// Example:
//
// class FooSpanExporter: public brpc::SpanExporter {
// public:
//     void DumpSpan(const RpczSpan* span) override {
//          // do dump span
//     }
//     ~FooSpanExporter() {}
// };
//
// brpc::RegisterSpanExporter(std::make_shared<FooSpanExporter>());
    
class SpanExporter {
public:
    virtual void DumpSpan(const TracingSpan* span) {}

    virtual ~SpanExporter() {}
};

//The following methods are thread-safe
void RegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter);
void UnRegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter);

class Span;
class SpanExporterManager {
public:
    void RegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter);
    void UnRegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter);

private:
friend class butil::GetLeakySingleton<SpanExporterManager>;
friend class Span;
    void DumpSpan(const TracingSpan* span);

    DISALLOW_COPY_AND_ASSIGN(SpanExporterManager);

    SpanExporterManager() {}
    ~SpanExporterManager() {}

    std::multiset<std::shared_ptr<SpanExporter>> _exporter_set;
    butil::Mutex _exporter_set_mutex;
};

} // namespace brpc


#endif // BRPC_SPAN_EXPORTER_H
