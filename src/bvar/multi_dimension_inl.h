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

// Date: 2021/11/17 10:57:43

#ifndef BVAR_MULTI_DIMENSION_INL_H
#define BVAR_MULTI_DIMENSION_INL_H

#include <gflags/gflags_declare.h>
#include "butil/compiler_specific.h"

namespace bvar {

DECLARE_int32(bvar_latency_p1);
DECLARE_int32(bvar_latency_p2);
DECLARE_int32(bvar_latency_p3);
DECLARE_uint32(max_multi_dimension_stats_count);

static const std::string ALLOW_UNUSED METRIC_TYPE_COUNTER = "counter";
static const std::string ALLOW_UNUSED METRIC_TYPE_SUMMARY = "summary";
static const std::string ALLOW_UNUSED METRIC_TYPE_HISTOGRAM = "histogram";
static const std::string ALLOW_UNUSED METRIC_TYPE_GAUGE = "gauge";

template <typename T, typename KeyType, bool Shared>
MultiDimension<T, KeyType, Shared>::MultiDimension(const key_type& labels)
    : Base(labels)
    , _max_stats_count(FLAGS_max_multi_dimension_stats_count) {
    _metric_map.Modify(init_flatmap);
}

template <typename T, typename KeyType, bool Shared>
MultiDimension<T, KeyType, Shared>::MultiDimension(const butil::StringPiece& name,
                                           const key_type& labels)
    : MultiDimension(labels) {
    this->expose(name);
}

template <typename T, typename KeyType, bool Shared>
MultiDimension<T, KeyType, Shared>::MultiDimension(const butil::StringPiece& prefix,
                                           const butil::StringPiece& name,
                                           const key_type& labels)
    : MultiDimension(labels) {
    this->expose_as(prefix, name);
}

template <typename T, typename KeyType, bool Shared>
MultiDimension<T, KeyType, Shared>::~MultiDimension() {
    this->hide();
    delete_stats();
}

template <typename T, typename KeyType, bool Shared>
size_t MultiDimension<T, KeyType, Shared>::init_flatmap(MetricMap& bg) {
    // size = 1 << 13
    CHECK_EQ(0, bg.init(8192, 80));
    return 1;
}

template <typename T, typename KeyType, bool Shared>
size_t MultiDimension<T, KeyType, Shared>::count_stats() {
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return 0;
    }
    return metric_map_ptr->size();
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
void MultiDimension<T, KeyType, Shared>::delete_stats(const K& labels_value) {
    if (is_valid_lables_value(labels_value)) {
        // Because there are two copies(foreground and background) in DBD,
        // we need to use an empty tmp_metric, get the deleted value of
        // second copy into tmp_metric, which can prevent the bvar object
        // from being deleted twice.
        op_value_type tmp_metric = NULL;
        auto erase_fn = [&labels_value, &tmp_metric](MetricMap& bg) {
            return bg.erase(labels_value, &tmp_metric);
        };
        _metric_map.Modify(erase_fn);
        if (tmp_metric) {
            delete_value(tmp_metric);
        }
    }
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::delete_stats() {
    // Because there are two copies(foreground and background) in DBD, we need to use an empty tmp_map,
    // swap two copies with empty, and get the value of second copy into tmp_map,
    // then traversal tmp_map and delete bvar object,
    // which can prevent the bvar object from being deleted twice.
    MetricMap tmp_map;
    CHECK_EQ(0, tmp_map.init(8192, 80));
    auto clear_fn = [&tmp_map](MetricMap& map) -> size_t {
        if (!tmp_map.empty()) {
            tmp_map.clear();
        }
        tmp_map.swap(map);
        return 1;
    };
    int ret = _metric_map.Modify(clear_fn);
    CHECK_EQ(1, ret);
    for (auto& kv : tmp_map) {
        delete_value(kv.second);
    }
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::list_stats(std::vector<key_type>* names) {
    if (names == NULL) {
        return;
    }
    names->clear();
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return;
    }
    names->reserve(metric_map_ptr->size());
    for (auto it = metric_map_ptr->begin(); it != metric_map_ptr->end(); ++it) {
        names->emplace_back(it->first);
    }
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
typename MultiDimension<T, KeyType, Shared>::value_ptr_type
MultiDimension<T, KeyType, Shared>::get_stats_impl(const K& labels_value) {
    if (!is_valid_lables_value(labels_value)) {
        return NULL;
    }
    MetricMapScopedPtr metric_map_ptr;
    if (_metric_map.Read(&metric_map_ptr) != 0) {
        LOG(ERROR) << "Fail to read dbd";
        return NULL;
    }

    auto it = metric_map_ptr->seek(labels_value);
    if (NULL == it) {
        return NULL;
    }
    return (*it);
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
typename MultiDimension<T, KeyType, Shared>::value_ptr_type
MultiDimension<T, KeyType, Shared>::get_stats_impl(
    const K& labels_value, STATS_OP stats_op, bool* do_write) {
    if (!is_valid_lables_value(labels_value)) {
        return NULL;
    }
    {
        MetricMapScopedPtr metric_map_ptr;
        if (0 != _metric_map.Read(&metric_map_ptr)) {
            LOG(ERROR) << "Fail to read dbd";
            return NULL;
        }

        auto it = metric_map_ptr->seek(labels_value);
        if (NULL != it) {
            return (*it);
        } else if (READ_ONLY == stats_op) {
            return NULL;
        }

        if (metric_map_ptr->size() > _max_stats_count) {
            LOG(ERROR) << "Too many stats seen, overflow detected, max stats count="
                       << _max_stats_count;
            return NULL;
        }
    }

    // Because DBD has two copies(foreground and background) MetricMap, both copies need to be modified,
    // In order to avoid new duplicate bvar object, need use cache_metric to cache the new bvar object,
    // In this way, when modifying the second copy, can directly use the cache_metric bvar object.
    op_value_type cache_metric = NULL;
    auto insert_fn = [this, &labels_value, &cache_metric, &do_write](MetricMap& bg) {
        auto bg_metric = bg.seek(labels_value);
        if (NULL != bg_metric) {
            cache_metric = *bg_metric;
            return 0;
        }
        if (do_write) {
            *do_write = true;
        }

        if (NULL == cache_metric) {
            cache_metric = new_value();
        }
        insert_metrics_map(bg, labels_value, cache_metric);
        return 1;
    };
    _metric_map.Modify(insert_fn);
    return cache_metric;
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::clear_stats() {
    delete_stats();
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
bool MultiDimension<T, KeyType, Shared>::has_stats(const K& labels_value) {
    return get_stats_impl(labels_value) != NULL;
}

template <typename T, typename KeyType, bool Shared>
template <typename U>
typename std::enable_if<!butil::is_same<LatencyRecorder, U>::value, size_t>::type
MultiDimension<T, KeyType, Shared>::dump_impl(Dumper* dumper, const DumpOptions* options) {
    std::vector<key_type> label_names;
    list_stats(&label_names);
    if (label_names.empty() || !dumper->dump_comment(this->name(), METRIC_TYPE_GAUGE)) {
        return 0;
    }
    size_t n = 0;
    for (auto &label_name : label_names) {
        value_ptr_type bvar = get_stats_impl(label_name);
        if (NULL == bvar) {
            continue;
        }
        std::ostringstream oss;
        bvar->describe(oss, options->quote_string);
        std::ostringstream oss_key;
        make_dump_key(oss_key, label_name);
        if (!dumper->dump_mvar(oss_key.str(), oss.str())) {
            continue;
        }
        n++;
    }
    return n;
}

template <typename T, typename KeyType, bool Shared>
template <typename U>
typename std::enable_if<butil::is_same<LatencyRecorder, U>::value, size_t>::type
MultiDimension<T, KeyType, Shared>::dump_impl(Dumper* dumper, const DumpOptions*) {
    std::vector<key_type> label_names;
    list_stats(&label_names);
    if (label_names.empty()) {
        return 0;
    }
    size_t n = 0;
    // To meet prometheus specification, we must guarantee no second TYPE line for one metric name

    // latency comment
    dumper->dump_comment(this->name() + "_latency", METRIC_TYPE_GAUGE);
    for (auto &label_name : label_names) {
        bvar::LatencyRecorder* bvar = get_stats_impl(label_name);
        if (!bvar) {
            continue;
        }

        // latency
        std::ostringstream oss_latency_key;
        make_dump_key(oss_latency_key, label_name, "_latency");
        if (dumper->dump_mvar(oss_latency_key.str(), std::to_string(bvar->latency()))) {
            n++;
        }
        // latency_percentiles
        // p1/p2/p3
        int latency_percentiles[3] {FLAGS_bvar_latency_p1, FLAGS_bvar_latency_p2, FLAGS_bvar_latency_p3};
        for (auto lp : latency_percentiles) {
            std::ostringstream oss_lp_key;
            make_dump_key(oss_lp_key, label_name, "_latency", lp);
            if (dumper->dump_mvar(oss_lp_key.str(), std::to_string(bvar->latency_percentile(lp / 100.0)))) {
                n++;
            }
        }
        // 999
        std::ostringstream oss_p999_key;
        make_dump_key(oss_p999_key, label_name, "_latency", 999);
        if (dumper->dump_mvar(oss_p999_key.str(), std::to_string(bvar->latency_percentile(0.999)))) {
            n++;
        }
        // 9999
        std::ostringstream oss_p9999_key;
        make_dump_key(oss_p9999_key, label_name, "_latency", 9999);
        if (dumper->dump_mvar(oss_p9999_key.str(), std::to_string(bvar->latency_percentile(0.9999)))) {
            n++;
        }
    }

    // max_latency comment
    dumper->dump_comment(this->name() + "_max_latency", METRIC_TYPE_GAUGE);
    for (auto &label_name : label_names) {
        LatencyRecorder* bvar = get_stats_impl(label_name);
        if (NULL == bvar) {
            continue;
        }
        std::ostringstream oss_max_latency_key;
        make_dump_key(oss_max_latency_key, label_name, "_max_latency");
        if (dumper->dump_mvar(oss_max_latency_key.str(), std::to_string(bvar->max_latency()))) {
            n++;
        }
    }

    // qps comment
    dumper->dump_comment(this->name() + "_qps", METRIC_TYPE_GAUGE);
    for (auto &label_name : label_names) {
        LatencyRecorder* bvar = get_stats_impl(label_name);
        if (NULL == bvar) {
            continue;
        }
        std::ostringstream oss_qps_key;
        make_dump_key(oss_qps_key, label_name, "_qps");
        if (dumper->dump_mvar(oss_qps_key.str(), std::to_string(bvar->qps()))) {
            n++;
        }
    }

    // count comment
    dumper->dump_comment(this->name() + "_count", METRIC_TYPE_COUNTER);
    for (auto &label_name : label_names) {
        LatencyRecorder* bvar = get_stats_impl(label_name);
        if (NULL == bvar) {
            continue;
        }
        std::ostringstream oss_count_key;
        make_dump_key(oss_count_key, label_name, "_count");
        if (dumper->dump_mvar(oss_count_key.str(), std::to_string(bvar->count()))) {
            n++;
        }
    }
    return n;
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::make_dump_key(std::ostream& os, const key_type& labels_value,
                                               const std::string& suffix, int quantile) {
    os << this->name();
    if (!suffix.empty()) {
        os << suffix;
    }
    make_labels_kvpair_string(os, labels_value, quantile);
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::make_labels_kvpair_string(
    std::ostream& os, const key_type& labels_value, int quantile) {
    os << "{";
    auto label_key = this->_labels.cbegin();
    auto label_value = labels_value.cbegin();
    char comma[2] = {'\0', '\0'};
    for (; label_key != this->_labels.cend() && label_value != labels_value.cend();
        label_key++, label_value++) {
        os << comma << label_key->c_str() << "=\"" << label_value->c_str() << "\"";
        comma[0] = ',';
    }
    if (quantile > 0) {
        os << comma << "quantile=\"" << quantile << "\"";
    }
    os << "}";
}

template <typename T, typename KeyType, bool Shared>
template <typename K>
bool MultiDimension<T, KeyType, Shared>::is_valid_lables_value(const K& labels_value) const {
    if (this->count_labels() != labels_value.size()) {
        LOG(ERROR) << "Invalid labels count" << this->count_labels()
                   << " != " << labels_value.size();
        return false;
    }
    return true;
}

template <typename T, typename KeyType, bool Shared>
void MultiDimension<T, KeyType, Shared>::describe(std::ostream& os) {
    os << "{\"name\" : \"" << this->name() << "\", \"labels\" : [";
    char comma[3] = {'\0', ' ', '\0'};
    for (auto& label : this->_labels) {
        os << comma << "\"" << label << "\"";
        comma[0] = ',';
    }
    os << "], \"stats_count\" : " << count_stats() <<  "}";
}

} // namespace bvar

#endif // BVAR_MULTI_DIMENSION_INL_H
