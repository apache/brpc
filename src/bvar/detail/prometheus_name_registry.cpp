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

#include <algorithm>
#include <pthread.h>
#include <gflags/gflags_declare.h>

#include "bvar/detail/prometheus_name_registry.h"

#include "butil/containers/flat_map.h"
#include "butil/logging.h"
#include "butil/scoped_lock.h"

namespace bvar {

DECLARE_bool(bvar_abort_on_same_name);
extern bool s_bvar_may_abort;

namespace detail {

static constexpr size_t PROMETHEUS_NAME_MAP_COUNT = 32;  // power of 2

typedef butil::FlatMap<std::string, const void*> PrometheusNameMap;

struct PrometheusNameMapWithLock : public PrometheusNameMap {
    PrometheusNameMapWithLock() {
        if (init(256) != 0) {
            LOG(WARNING) << "Fail to init PrometheusNameMap";
        }
        pthread_mutex_init(&mutex, nullptr);
    }

    pthread_mutex_t mutex;
};

// Sorts and deduplicates map indices before locking them in ascending order.
// Different name sets therefore cannot acquire overlapping maps in opposite
// orders, keeping multi-name reservations atomic without deadlocking.
class PrometheusNameMapLocks {
public:
    PrometheusNameMapLocks(PrometheusNameMapWithLock* maps,
                           const std::vector<size_t>& map_indices)
        : _maps(maps), _map_indices(map_indices) {
        std::sort(_map_indices.begin(), _map_indices.end());
        _map_indices.erase(std::unique(_map_indices.begin(), _map_indices.end()),
                           _map_indices.end());
        for (size_t index : _map_indices) {
            pthread_mutex_lock(&_maps[index].mutex);
        }
    }

    DISALLOW_COPY_AND_ASSIGN(PrometheusNameMapLocks);

    ~PrometheusNameMapLocks() {
        for (auto it = _map_indices.rbegin(); it != _map_indices.rend(); ++it) {
            pthread_mutex_unlock(&_maps[*it].mutex);
        }
    }

private:

    PrometheusNameMapWithLock* _maps;
    std::vector<size_t> _map_indices;
};

static pthread_once_t s_name_maps_once = PTHREAD_ONCE_INIT;
static PrometheusNameMapWithLock* s_name_maps = nullptr;

static void init_name_maps() {
    // Intentionally leaked so static bvars can use the registry during global
    // construction and destruction without depending on object order.
    s_name_maps = new PrometheusNameMapWithLock[PROMETHEUS_NAME_MAP_COUNT];
}

static PrometheusNameMapWithLock* name_maps() {
    pthread_once(&s_name_maps_once, init_name_maps);
    return s_name_maps;
}

static size_t name_map_index(const std::string& name) {
    PrometheusNameMap::hasher hasher;
    size_t hash = hasher(name);
    return hash & (PROMETHEUS_NAME_MAP_COUNT - 1);
}

static std::vector<std::string> deduplicate_names(
    const std::vector<std::string>& names) {
    std::vector<std::string> unique_names(names);
    std::sort(unique_names.begin(), unique_names.end());
    unique_names.erase(std::unique(unique_names.begin(), unique_names.end()),
                       unique_names.end());
    return unique_names;
}

static std::vector<size_t> collect_name_map_indices(const std::vector<std::string>& names) {
    std::vector<size_t> indices;
    indices.reserve(names.size());
    for (auto& name : names) {
        indices.push_back(name_map_index(name));
    }
    return indices;
}

static void report_conflict(const std::string& name) {
    RELEASE_ASSERT_VERBOSE(!FLAGS_bvar_abort_on_same_name,
                           "Abort due to name conflict");
    if (!s_bvar_may_abort) {
        s_bvar_may_abort = true;
    }
    LOG(ERROR) << "Prometheus metric name `" << name << "' is already exposed";
}

bool reserve_prometheus_names(const void* owner, const std::vector<std::string>& names) {
    if (owner == nullptr) {
        return false;
    }

    // A composite family may repeat its root (for example a Histogram whose
    // family suffix is empty). Treat duplicates from one owner as one name.
    std::vector<std::string> unique_names = deduplicate_names(names);
    std::vector<size_t> map_indices = collect_name_map_indices(unique_names);
    PrometheusNameMapWithLock* maps = name_maps();
    bool has_conflict = false;
    std::string conflict_name;
    {
        PrometheusNameMapLocks name_locks(maps, map_indices);
        for (auto& name : unique_names) {
            PrometheusNameMapWithLock& name_map = maps[name_map_index(name)];
            auto name_owner = name_map.seek(name);
            if (name_owner != nullptr) {
                has_conflict = true;
                conflict_name = name;
                break;
            }
        }

        if (!has_conflict) {
            for (auto& name : unique_names) {
                PrometheusNameMapWithLock& name_map =
                    maps[name_map_index(name)];
                name_map[name] = owner;
            }
        }
    }
    if (has_conflict) {
        report_conflict(conflict_name);
        return false;
    }
    return true;
}

void release_prometheus_names(const void* owner, const std::vector<std::string>& names) {
    if (owner == nullptr) {
        return;
    }

    std::vector<std::string> unique_names = deduplicate_names(names);
    std::vector<size_t> map_indices = collect_name_map_indices(unique_names);
    PrometheusNameMapWithLock* maps = name_maps();
    PrometheusNameMapLocks name_locks(maps, map_indices);
    for (auto& name : unique_names) {
        PrometheusNameMapWithLock& name_map = maps[name_map_index(name)];
        auto name_owner = name_map.seek(name);
        // A stale or duplicate cleanup must not erase a name that has already
        // been released and reserved by another variable.
        if (name_owner != nullptr && *name_owner == owner) {
            name_map.erase(name);
        }
    }
}

}  // namespace detail
}  // namespace bvar
