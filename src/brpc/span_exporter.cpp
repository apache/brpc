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

#include <mutex>
#include "butil/logging.h"
#include "brpc/span_exporter.h"

namespace brpc {

static butil::atomic<int> g_span_exporter_index;

SpanExporter::SpanExporter() 
    : _id(g_span_exporter_index.fetch_add(1, butil::memory_order_relaxed)) {
}

SpanExporter::SpanExporter(const std::string& name) 
    : _name(name)
    , _id(g_span_exporter_index.fetch_add(1, butil::memory_order_relaxed)) {
}

void SpanExporter::Describe(std::ostream& os) const {
    if (_name.empty()) {
        os << "SpanExporter";
    } else {
        os << _name;
    }
    os << "[id =" << _id << ']';
}

void SpanExporterManager::RegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    std::unique_lock<butil::Mutex> lock_guard(_exporter_set_mutex);
    auto it = _exporter_set.find(span_exporter);
    if (it != _exporter_set.end()) {
        std::ostringstream os;
        (*it)->Describe(os);
        LOG(WARNING) << "SpanExport `" << os.str()
                     << "' already registered";
        return;
    }
    _exporter_set.insert(std::move(span_exporter));
}

void SpanExporterManager::UnRegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    std::unique_lock<butil::Mutex> lock_guard(_exporter_set_mutex);
    if (_exporter_set.find(span_exporter) == _exporter_set.end()) {
        std::ostringstream os;
        span_exporter->Describe(os);
        LOG(WARNING) << "SpanExport `" << os.str()
                     << "' not registered yet";
        return;
    }
    _exporter_set.erase(std::move(span_exporter));
}

void SpanExporterManager::DumpSpan(const TracingSpan* span) {
    std::set<std::shared_ptr<SpanExporter>> exporter_copy;
    {
        std::unique_lock<butil::Mutex> lock_guard(_exporter_set_mutex);
        exporter_copy = _exporter_set;
    }
    for (const std::shared_ptr<SpanExporter>& exporter : exporter_copy) {
        exporter->DumpSpan(span);
    }
}

void RegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    butil::get_leaky_singleton<SpanExporterManager>()->RegisterSpanExporter(
        std::move(span_exporter));
}

void UnRegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    butil::get_leaky_singleton<SpanExporterManager>()->UnRegisterSpanExporter(
        std::move(span_exporter));
}


} // namespace brpc
