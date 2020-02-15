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

// Date 2014/09/22 11:57:43

#ifndef  BVAR_STATUS_H
#define  BVAR_STATUS_H

#include <string>                       // std::string
#include "butil/atomicops.h"
#include "butil/type_traits.h"
#include "butil/string_printf.h"
#include "butil/synchronization/lock.h"
#include "bvar/detail/is_atomical.h"
#include "bvar/variable.h"

namespace bvar {

// Display a rarely or periodically updated value.
// Usage:
//   bvar::Status<int> foo_count1(17);
//   foo_count1.expose("my_value");
//
//   bvar::Status<int> foo_count2;
//   foo_count2.set_value(17);
//   
//   bvar::Status<int> foo_count3("my_value", 17);
template <typename T, typename Enabler = void>
class Status : public Variable {
public:
    Status() {}
    Status(const T& value) : _value(value) {}
    Status(const butil::StringPiece& name, const T& value) : _value(value) {
        this->expose(name);
    }
    Status(const butil::StringPiece& prefix,
           const butil::StringPiece& name, const T& value) : _value(value) {
        this->expose_as(prefix, name);
    }
    // Calling hide() manually is a MUST required by Variable.
    ~Status() { hide(); }

    void describe(std::ostream& os, bool /*quote_string*/) const override {
        os << get_value();
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const override {
        butil::AutoLock guard(_lock);
        *value = _value;
    }
#endif

    T get_value() const {
        butil::AutoLock guard(_lock);
        const T res = _value;
        return res;
    }

    void set_value(const T& value) {
        butil::AutoLock guard(_lock);
        _value = value;
    }

private:
    T _value;
    // We use lock rather than butil::atomic for generic values because
    // butil::atomic requires the type to be memcpy-able (POD basically)
    mutable butil::Lock _lock;
};

template <typename T>
class Status<T, typename butil::enable_if<detail::is_atomical<T>::value>::type>
    : public Variable {
public:
    struct PlaceHolderOp {
        void operator()(T&, const T&) const {}
    };
    class SeriesSampler : public detail::Sampler {
    public:
        typedef typename butil::conditional<
        true, detail::AddTo<T>, PlaceHolderOp>::type Op;
        explicit SeriesSampler(Status* owner)
            : _owner(owner), _series(Op()) {}
        void take_sample() { _series.append(_owner->get_value()); }
        void describe(std::ostream& os) { _series.describe(os, NULL); }
    private:
        Status* _owner;
        detail::Series<T, Op> _series;
    };

public:
    Status() : _series_sampler(NULL) {}
    Status(const T& value) : _value(value), _series_sampler(NULL) { }
    Status(const butil::StringPiece& name, const T& value)
        : _value(value), _series_sampler(NULL) {
        this->expose(name);
    }
    Status(const butil::StringPiece& prefix,
           const butil::StringPiece& name, const T& value)
        : _value(value), _series_sampler(NULL) {
        this->expose_as(prefix, name);
    }
    ~Status() {
        hide();
        if (_series_sampler) {
            _series_sampler->destroy();
            _series_sampler = NULL;
        }
    }

    void describe(std::ostream& os, bool /*quote_string*/) const override {
        os << get_value();
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const override {
        *value = get_value();
    }
#endif
    
    T get_value() const {
        return _value.load(butil::memory_order_relaxed);
    }
    
    void set_value(const T& value) {
        _value.store(value, butil::memory_order_relaxed);
    }

    int describe_series(std::ostream& os, const SeriesOptions& options) const override {
        if (_series_sampler == NULL) {
            return 1;
        }
        if (!options.test_only) {
            _series_sampler->describe(os);
        }
        return 0;
    }

protected:
    int expose_impl(const butil::StringPiece& prefix,
                    const butil::StringPiece& name,
                    DisplayFilter display_filter) override {
        const int rc = Variable::expose_impl(prefix, name, display_filter);
        if (rc == 0 &&
            _series_sampler == NULL &&
            FLAGS_save_series) {
            _series_sampler = new SeriesSampler(this);
            _series_sampler->schedule();
        }
        return rc;
    }

private:
    butil::atomic<T> _value;
    SeriesSampler* _series_sampler;
};

// Specialize for std::string, adding a printf-style set_value().
template <>
class Status<std::string, void> : public Variable {
public:
    Status() {}
    Status(const butil::StringPiece& name, const char* fmt, ...) {
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            butil::string_vprintf(&_value, fmt, ap);
            va_end(ap);
        }
        expose(name);
    }
    Status(const butil::StringPiece& prefix,
           const butil::StringPiece& name, const char* fmt, ...) {
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            butil::string_vprintf(&_value, fmt, ap);
            va_end(ap);
        }
        expose_as(prefix, name);
    }

    ~Status() { hide(); }

    void describe(std::ostream& os, bool quote_string) const override {
        if (quote_string) {
            os << '"' << get_value() << '"';
        } else {
            os << get_value();
        }
    }

    std::string get_value() const {
        butil::AutoLock guard(_lock);
        return _value;
    }

#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const override {
        *value = get_value();
    }
#endif
    
    void set_value(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        {
            butil::AutoLock guard(_lock);
            butil::string_vprintf(&_value, fmt, ap);
        }
        va_end(ap);
    }

    void set_value(const std::string& s) {
        butil::AutoLock guard(_lock);
        _value = s;
    }

private:
    std::string _value;
    mutable butil::Lock _lock;
};

}  // namespace bvar

#endif  //BVAR_STATUS_H
