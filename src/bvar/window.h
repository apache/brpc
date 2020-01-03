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

// Date: Wed Jul 29 23:25:43 CST 2015

#ifndef  BVAR_WINDOW_H
#define  BVAR_WINDOW_H

#include <limits>                                 // std::numeric_limits
#include <math.h>                                 // round
#include <gflags/gflags_declare.h>
#include "butil/logging.h"                         // LOG
#include "bvar/detail/sampler.h"
#include "bvar/detail/series.h"
#include "bvar/variable.h"

namespace bvar {

DECLARE_int32(bvar_dump_interval);

enum SeriesFrequency {
    SERIES_IN_WINDOW = 0,
    SERIES_IN_SECOND = 1
};

namespace detail {
// Just for constructor reusing of Window<>
template <typename R, SeriesFrequency series_freq>
class WindowBase : public Variable {
public:
    typedef typename R::value_type value_type;
    typedef typename R::sampler_type sampler_type;

    class SeriesSampler : public detail::Sampler {
    public:
        struct Op {
            explicit Op(R* var) : _var(var) {}
            void operator()(value_type& v1, const value_type& v2) const {
                _var->op()(v1, v2);
            }
        private:
            R* _var;
        };
        SeriesSampler(WindowBase* owner, R* var)
            : _owner(owner), _series(Op(var)) {}
        ~SeriesSampler() {}
        void take_sample() override {
            if (series_freq == SERIES_IN_SECOND) {
                // Get one-second window value for PerSecond<>, otherwise the
                // "smoother" plot may hide peaks.
                _series.append(_owner->get_value(1));
            } else {
                // Get the value inside the full window. "get_value(1)" is 
                // incorrect when users intend to see aggregated values of
                // the full window in the plot.
                _series.append(_owner->get_value());
            }
        }
        void describe(std::ostream& os) { _series.describe(os, NULL); }
    private:
        WindowBase* _owner;
        detail::Series<value_type, Op> _series;
    };
    
    WindowBase(R* var, time_t window_size)
        : _var(var)
        , _window_size(window_size > 0 ? window_size : FLAGS_bvar_dump_interval)
        , _sampler(var->get_sampler())
        , _series_sampler(NULL) {
        CHECK_EQ(0, _sampler->set_window_size(_window_size));
    }
    
    ~WindowBase() {
        hide();
        if (_series_sampler) {
            _series_sampler->destroy();
            _series_sampler = NULL;
        }
    }

    bool get_span(time_t window_size, detail::Sample<value_type>* result) const {
        return _sampler->get_value(window_size, result);
    }

    bool get_span(detail::Sample<value_type>* result) const {
        return get_span(_window_size, result);
    }

    virtual value_type get_value(time_t window_size) const {
        detail::Sample<value_type> tmp;
        if (get_span(window_size, &tmp)) {
            return tmp.data;
        }
        return value_type();
    }

    value_type get_value() const { return get_value(_window_size); }
    
    void describe(std::ostream& os, bool quote_string) const override {
        if (butil::is_same<value_type, std::string>::value && quote_string) {
            os << '"' << get_value() << '"';
        } else {
            os << get_value();
        }
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const override { *value = get_value(); }
#endif

    time_t window_size() const { return _window_size; }

    int describe_series(std::ostream& os, const SeriesOptions& options) const override {
        if (_series_sampler == NULL) {
            return 1;
        }
        if (!options.test_only) {
            _series_sampler->describe(os);
        }
        return 0;
    }

    void get_samples(std::vector<value_type> *samples) const {
        samples->clear();
        samples->reserve(_window_size);
        return _sampler->get_samples(samples, _window_size);
    }

protected:
    int expose_impl(const butil::StringPiece& prefix,
                    const butil::StringPiece& name,
                    DisplayFilter display_filter) override {
        const int rc = Variable::expose_impl(prefix, name, display_filter);
        if (rc == 0 &&
            _series_sampler == NULL &&
            FLAGS_save_series) {
            _series_sampler = new SeriesSampler(this, _var);
            _series_sampler->schedule();
        }
        return rc;
    }

    R* _var;
    time_t _window_size;
    sampler_type* _sampler;
    SeriesSampler* _series_sampler;
};

}  // namespace detail

// Get data within a time window.
// The time unit is 1 second fixed.
// Window relies on other bvar which should be constructed before this window
// and destructs after this window.

// R must:
// - have get_sampler() (not require thread-safe)
// - defined value_type and sampler_type
template <typename R, SeriesFrequency series_freq = SERIES_IN_WINDOW>
class Window : public detail::WindowBase<R, series_freq> {
    typedef detail::WindowBase<R, series_freq> Base;
    typedef typename R::value_type value_type;
    typedef typename R::sampler_type sampler_type;
public:
    // Different from PerSecond, we require window_size here because get_value
    // of Window is largely affected by window_size while PerSecond is not.
    Window(R* var, time_t window_size) : Base(var, window_size) {}
    Window(const butil::StringPiece& name, R* var, time_t window_size)
        : Base(var, window_size) {
        this->expose(name);
    }
    Window(const butil::StringPiece& prefix,
           const butil::StringPiece& name, R* var, time_t window_size)
        : Base(var, window_size) {
        this->expose_as(prefix, name);
    }
};

// Get data per second within a time window.
// The only difference between PerSecond and Window is that PerSecond divides
// the data by time duration.
template <typename R>
class PerSecond : public detail::WindowBase<R, SERIES_IN_SECOND> {
    typedef detail::WindowBase<R, SERIES_IN_SECOND> Base;
    typedef typename R::value_type value_type;
    typedef typename R::sampler_type sampler_type;
public:
    // If window_size is non-positive or absent, use FLAGS_bvar_dump_interval.
    PerSecond(R* var) : Base(var, -1) {}
    PerSecond(R* var, time_t window_size) : Base(var, window_size) {}
    PerSecond(const butil::StringPiece& name, R* var) : Base(var, -1) {
        this->expose(name);
    }
    PerSecond(const butil::StringPiece& name, R* var, time_t window_size)
        : Base(var, window_size) {
        this->expose(name);
    }
    PerSecond(const butil::StringPiece& prefix,
              const butil::StringPiece& name, R* var)
        : Base(var, -1) {
        this->expose_as(prefix, name);
    }
    PerSecond(const butil::StringPiece& prefix,
              const butil::StringPiece& name, R* var, time_t window_size)
        : Base(var, window_size) {
        this->expose_as(prefix, name);
    }

    value_type get_value(time_t window_size) const override {
        detail::Sample<value_type> s;
        this->get_span(window_size, &s);
        // We may test if the multiplication overflows and use integral ops
        // if possible. However signed/unsigned 32-bit/64-bit make the solution
        // complex. Since this function is not called often, we use floating
        // point for simplicity.
        if (s.time_us <= 0) {
            return static_cast<value_type>(0);
        }
        if (butil::is_floating_point<value_type>::value) {
            return static_cast<value_type>(s.data * 1000000.0 / s.time_us);
        } else {
            return static_cast<value_type>(round(s.data * 1000000.0 / s.time_us));
        }
    }
};

}  // namespace bvar

#endif  //BVAR_WINDOW_H
