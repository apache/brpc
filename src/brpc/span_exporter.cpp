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

void SpanExporterManager::RegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    std::unique_lock<butil::Mutex> lock_guard(_exporter_list_mutex);
    _exporter_list.push_back(std::move(span_exporter));
}

void SpanExporterManager::UnRegisterSpanExporter(std::shared_ptr<SpanExporter> span_exporter) {
    std::unique_lock<butil::Mutex> lock_guard(_exporter_list_mutex);
    auto it = std::find(_exporter_list.begin(), _exporter_list.end(), span_exporter);
    if (it == _exporter_list.end()) {
        LOG(WARNING) << "SpanExport not registered yet";
        return;
    }
    swap(*it, _exporter_list.back());
    _exporter_list.erase(--_exporter_list.end());
}

void SpanExporterManager::DumpSpan(const TracingSpan* span) {
    std::vector<std::shared_ptr<SpanExporter>> exporter_copy;
    {
        std::unique_lock<butil::Mutex> lock_guard(_exporter_list_mutex);
        exporter_copy = _exporter_list;
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
