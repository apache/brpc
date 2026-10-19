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

#include <algorithm>                    // std::adjacent_find
#include <functional>                   // std::greater_equal
#include <iterator>                     // std::next
#include <string>                       // std::string
#include "butil/float_util.h"            // butil::IsFinite
#include "butil/logging.h"               // LOG
#include "butil/strings/string_number_conversions.h" // butil::DoubleToString
#include "bvar/histogram.h"

namespace bvar {

// The +Inf/-Inf/NaN of the prometheus text format are not json, so describe()
// cannot borrow that spelling: it writes a non representable number as `null`,
// which is what JSON.stringify() does with one. Finite values still go through
// prometheus_double_to_string() so that the two outputs agree digit for digit
// and neither follows LC_NUMERIC.
static std::string json_double_to_string(double value) {
    if (BAIDU_UNLIKELY(!butil::IsFinite(value))) {
        return "null";
    }
    return detail::prometheus_double_to_string(value);
}

Histogram::BucketSchema::BucketSchema(std::initializer_list<double> bounds)
    : _bounds(bounds) {
    validate_bounds();
}

Histogram::BucketSchema::BucketSchema(const std::vector<double>& bounds)
    : _bounds(bounds) {
    validate_bounds();
}

void Histogram::BucketSchema::validate_bounds() {
    for (auto it = _bounds.begin(); it != _bounds.end();) {
        if (!butil::IsFinite(*it)) {
            LOG(ERROR) << "Bucket bounds must be finite, dropping " << *it;
            it = _bounds.erase(it);
        } else {
            ++it;
        }
    }

    // Drop the bounds that do not keep the sequence strictly ascending.
    auto it = _bounds.begin();
    while ((it = std::adjacent_find(it, _bounds.end(),
                                    std::greater_equal<>())) != _bounds.end()) {
        auto bad = std::next(it);
        LOG(ERROR) << "Bucket bounds must be strictly ascending, dropping "
                   << *bad << " which is not greater than " << *it;
        _bounds.erase(bad);
    }
    if (_bounds.size() >= MAX_HISTOGRAM_BUCKETS) {
        // Shrinking a vector drops the tail, keeping the tightest bounds.
        LOG(ERROR) << "A histogram takes at most " << MAX_HISTOGRAM_BUCKETS - 1
                   << " bounds, dropping the "
                   << _bounds.size() - (MAX_HISTOGRAM_BUCKETS - 1)
                   << " ones past the limit";
        _bounds.resize(MAX_HISTOGRAM_BUCKETS - 1);
    }
    if (_bounds.empty()) {
        // A schema without any bound would send everything into the +Inf
        // bucket, which no quantile can be read off. Keep the histogram
        // usable rather than aborting the process over a misconfiguration.
        LOG(ERROR) << "A histogram needs at least one bound, falling back to 1";
        _bounds.push_back(1.0);
    }
}

std::ostream& operator<<(std::ostream& os, const Histogram::Value& v) {
    os << "{\"count\":" << v.num << ",\"sum\":"
       << json_double_to_string(v.sum)
       << ",\"counts\":[";
    size_t nbuckets = std::min(v.num_buckets, MAX_HISTOGRAM_BUCKETS);
    for (size_t i = 0; i < nbuckets; ++i) {
        if (i != 0) {
            os << ',';
        }
        os << v.counts[i];
    }
    return os << "]}";
}

Histogram::Histogram(const BucketSchema& schema)
    : _schema(schema)
#if WITH_BABYLON_COUNTER
    , _storage(std::make_shared<detail::HistogramStorage>(schema.num_buckets()))
#else
    // Both identities carry `num_buckets` so that a value combined out of no
    // agent at all still knows how wide it is.
    , _combiner(std::make_shared<combiner_type>(value_type(schema.num_buckets()),
                                                  value_type(schema.num_buckets())))
#endif // WITH_BABYLON_COUNTER
    , _sampler(nullptr) {
}

Histogram::Histogram(const butil::StringPiece& name, const BucketSchema& schema)
    : Histogram(schema) {
    expose(name);
}

Histogram::Histogram(const butil::StringPiece& prefix,
                     const butil::StringPiece& name,
                     const BucketSchema& schema)
    : Histogram(schema) {
    expose_as(prefix, name);
}

Histogram::~Histogram() {
    // Calling hide() manually is a MUST required by Variable.
    hide();
    if (_sampler != nullptr) {
        _sampler->destroy();
    }
}

Histogram& Histogram::operator<<(double value) {
    if (BAIDU_UNLIKELY(!butil::IsFinite(value))) {
        LOG_EVERY_SECOND(WARNING) << "Ignoring non-finite value=" << value
                                  << " recorded into Histogram(" << name() << ')';
        return *this;
    }
#if WITH_BABYLON_COUNTER
    _storage->add(_schema.index_of(value), value);
#else
    agent_type* agent = _combiner->get_or_create_tls_agent();
    if (BAIDU_UNLIKELY(agent == nullptr)) {
        LOG(FATAL) << "Fail to create agent";
        return *this;
    }
    // `_schema` outlives the call, the op only borrows it to find the bucket.
    agent->element.modify(detail::AddSampleToHistogram(&_schema), value);
#endif // WITH_BABYLON_COUNTER
    return *this;
}

Histogram::value_type Histogram::get_value() const {
#if WITH_BABYLON_COUNTER
    return _storage->combine_agents();
#else
    return _combiner->combine_agents();
#endif // WITH_BABYLON_COUNTER
}

Histogram::sampler_type* Histogram::get_sampler() {
    if (_sampler == nullptr) {
        _sampler = new sampler_type(this);
        _sampler->set_debug_name(name());
        _sampler->schedule();
    }
    return _sampler;
}

int Histogram::expose_impl(const butil::StringPiece& prefix,
                           const butil::StringPiece& name,
                           DisplayFilter display_filter) {
    int rc = Variable::expose_impl(prefix, name, display_filter);
    if (rc == 0 && _sampler != nullptr) {
        _sampler->set_debug_name(this->name());
    }
    return rc;
}

void Histogram::describe(std::ostream& os, bool /*quote_string*/) const {
    value_type v = get_value();
    os << "{\"count\":" << v.num
       << ",\"sum\":" << json_double_to_string(v.sum)
       << ",\"bounds\":[";
    for (size_t i = 0; i < _schema.num_bounds(); ++i) {
        if (i != 0) {
            os << ',';
        }
        // validate_bounds() already dropped the non finite ones, so this only
        // keeps the whole json on one spelling rule.
        os << json_double_to_string(_schema.bound_at(i));
    }
    os << "],\"counts\":[";
    for (size_t i = 0; i < _schema.num_buckets(); ++i) {
        if (i != 0) {
            os << ',';
        }
        os << v.counts[i];
    }
    os << "]}";
}

const std::vector<MetricFamily>& Histogram::list_metric_families() {
    // Deliberately leaked. A function local static registers its destructor
    // with atexit on the first call, which is whenever the first Histogram is
    // dumped or exposed, and that can be later than the construction of a
    // static object which reads this from its own destructor.
    static auto families = new std::vector<MetricFamily>{
        {"", "histogram", {"le"}},
    };
    return *families;
}

bool Histogram::dump(Dumper* dumper, const DumpOptions&,
                     const std::string& name) const {
    // One comment for the whole family, then one metric per bucket.
    // Going through Dumper::dump() instead would make the prometheus
    // dumper prepend a "# TYPE ... gauge" to every single bucket.
    if (!dumper->dump_comment(name, list_metric_families()[0].type)) {
        return false;
    }
    return dump_samples(dumper, 0, name, butil::StringPiece());
}

// Builds the name of one sample into `key': `name' + `suffix', then the brace
// group holding `labels' (the labels of the enclosing MultiDimension, if any)
// followed by `le' when the sample is a bucket. Namely
//     foo_bucket{method="echo",le="10"}   or   foo_sum{method="echo"}
// A sample with neither gets no braces at all: `foo_sum'.
static void make_sample_key(std::string* key, const std::string& name,
                            const char* suffix,
                            butil::StringPiece labels,
                            butil::StringPiece le) {
    key->assign(name);
    key->append(suffix);
    if (labels.empty() && le.empty()) {
        return;
    }
    key->push_back('{');
    key->append(labels.data(), labels.size());
    if (!le.empty()) {
        if (!labels.empty()) {
            key->push_back(',');
        }
        key->append("le=\"");
        key->append(le.data(), le.size());
        key->push_back('"');
    }
    key->push_back('}');
}

bool Histogram::dump_samples(Dumper* dumper, size_t /*family_index*/,
                             const std::string& name,
                             butil::StringPiece labels) const {
    value_type v = get_value();
    std::string key;
    std::string bound;
    // Prometheus wants the buckets cumulative: foo_bucket{le="20"} counts
    // everything not greater than 20, not just what falls between 10 and 20.
    // The last one therefore holds every recorded value, which is exactly the
    // `le="+Inf" must equal _count` rule of the format.
    uint64_t cumulative = 0;
    for (size_t i = 0; i < _schema.num_buckets(); ++i) {
        cumulative += v.counts[i];
        if (_schema.is_inf_bucket(i)) {
            bound = "+Inf";
        } else {
            bound = detail::prometheus_double_to_string(_schema.bound_at(i));
        }
        make_sample_key(&key, name, "_bucket", labels, bound);
        if (!dumper->dump_mvar(key, butil::Uint64ToString(cumulative))) {
            return false;
        }
    }
    make_sample_key(&key, name, "_sum", labels, butil::StringPiece());
    if (!dumper->dump_mvar(key, detail::prometheus_double_to_string(v.sum))) {
        return false;
    }
    make_sample_key(&key, name, "_count", labels, butil::StringPiece());
    return dumper->dump_mvar(key, butil::Int64ToString(v.num));
}

}  // namespace bvar
