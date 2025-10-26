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

// Date 2014/09/24 16:01:08

#ifndef  BVAR_REDUCER_H
#define  BVAR_REDUCER_H

#include <limits>                                 // std::numeric_limits
#include "butil/logging.h"                         // LOG()
#include "butil/type_traits.h"                     // butil::add_cr_non_integral
#include "butil/class_name.h"                      // class_name_str
#include "bvar/variable.h"                        // Variable
#include "bvar/detail/combiner.h"                 // detail::AgentCombiner
#include "bvar/detail/sampler.h"                  // ReducerSampler
#include "bvar/detail/series.h"
#include "bvar/window.h"
#if WITH_BABYLON_COUNTER
#include "babylon/concurrent/counter.h"
#endif // WITH_BABYLON_COUNTER

namespace bvar {

namespace detail {
template<typename O, typename T, typename Op>
class SeriesSamplerImpl : public Sampler {
public:
    SeriesSamplerImpl(O* owner, const Op& op)
        : _owner(owner), _series(op) {}
    void take_sample() override { _series.append(_owner->get_value()); }
    void describe(std::ostream& os) { _series.describe(os, NULL); }

private:
    O* _owner;
    Series<T, Op> _series;
};

#if WITH_BABYLON_COUNTER
template<typename T, typename Counter, typename Op, typename InvOp>
class BabylonVariable: public Variable {
public:
    typedef ReducerSampler<BabylonVariable, T, Op, InvOp> sampler_type;
    typedef SeriesSamplerImpl<BabylonVariable, T, Op> series_sampler_type;

    BabylonVariable() = default;

    template<typename U = T, typename std::enable_if<
        !std::is_constructible<Counter, U>::value, bool>::type = false>
    BabylonVariable(U) {}
    // For Maxer.
    template<typename U = T, typename std::enable_if<
        std::is_constructible<Counter, U>::value, bool>::type = false>
    BabylonVariable(U default_value) : _counter(default_value) {}

    DISALLOW_COPY_AND_MOVE(BabylonVariable);

    ~BabylonVariable() override {
        hide();
        if (NULL != _sampler) {
            _sampler->destroy();
        }
        if (NULL != _series_sampler) {
            _series_sampler->destroy();
        }
    }

    BabylonVariable& operator<<(T value) {
        _counter << value;
        return *this;
    }

    sampler_type* get_sampler() {
        if (NULL == _sampler) {
            _sampler = new sampler_type(this);
            _sampler->schedule();
        }
        return _sampler;
    }

    T get_value() const {
        return _counter.value();
    }

    T reset() {
        if (BAIDU_UNLIKELY((!butil::is_same<VoidOp, InvOp>::value))) {
            CHECK(false) << "You should not call Reducer<" << butil::class_name_str<T>()
                         << ", " << butil::class_name_str<Op>() << ">::get_value() when a"
                         << " Window<> is used because the operator does not have inverse.";
            return get_value();
        }

        T result = _counter.value();
        _counter.reset();
        return result;
    }

    bool valid() const { return true; }

    const Op& op() const { return _op; }
    const InvOp& inv_op() const { return _inv_op;}

    void describe(std::ostream& os, bool quote_string) const override {
        if (butil::is_same<T, std::string>::value && quote_string) {
            os << '"' << get_value() << '"';
        } else {
            os << get_value();
        }
    }

    int describe_series(std::ostream& os, const SeriesOptions& options) const override {
        if (NULL == _series_sampler) {
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
        if (rc == 0 && NULL == _series_sampler &&
            !butil::is_same<InvOp, VoidOp>::value &&
            !butil::is_same<T, std::string>::value &&
            FLAGS_save_series) {
            _series_sampler = new series_sampler_type(this, _op);
            _series_sampler->schedule();
        }
        return rc;
    }

private:
    Counter _counter;
    sampler_type* _sampler{NULL};
    series_sampler_type* _series_sampler{NULL};
    Op _op;
    InvOp _inv_op;
};
#endif // WITH_BABYLON_COUNTER
} // namespace detail

// Reduce multiple values into one with `Op': e1 Op e2 Op e3 ...
// `Op' shall satisfy:
//   - associative:     a Op (b Op c) == (a Op b) Op c
//   - commutative:     a Op b == b Op a;
//   - no side effects: a Op b never changes if a and b are fixed. 
// otherwise the result is undefined.
//
// For performance issues, we don't let Op return value, instead it shall
// set the result to the first parameter in-place. Namely to add two values,
// "+=" should be implemented rather than "+".
//
// Reducer works for non-primitive T which satisfies:
//   - T() should be the identity of Op.
//   - stream << v should compile and put description of v into the stream
// Example:
// class MyType {
// friend std::ostream& operator<<(std::ostream& os, const MyType&);
// public:
//     MyType() : _x(0) {}
//     explicit MyType(int x) : _x(x) {}
//     void operator+=(const MyType& rhs) const {
//         _x += rhs._x;
//     }
// private:
//     int _x;
// };
// std::ostream& operator<<(std::ostream& os, const MyType& value) {
//     return os << "MyType{" << value._x << "}";
// }
// bvar::Adder<MyType> my_type_sum;
// my_type_sum << MyType(1) << MyType(2) << MyType(3);
// LOG(INFO) << my_type_sum;  // "MyType{6}"

template <typename T, typename Op, typename InvOp = detail::VoidOp>
class Reducer : public Variable {
public:
    typedef detail::AgentCombiner<T, T, Op> combiner_type;
    typedef typename combiner_type::self_shared_type shared_combiner_type;
    typedef typename combiner_type::Agent agent_type;
    typedef detail::ReducerSampler<Reducer, T, Op, InvOp> sampler_type;
    typedef detail::SeriesSamplerImpl<Reducer, T, Op> SeriesSampler;

    // The `identify' must satisfy: identity Op a == a
    explicit Reducer(typename butil::add_cr_non_integral<T>::type identity = T(),
                     const Op& op = Op(), const InvOp& inv_op = InvOp())
        : _combiner(std::make_shared<combiner_type>(identity, identity, op))
        , _sampler(NULL) , _series_sampler(NULL) , _inv_op(inv_op) {}

    ~Reducer() override {
        // Calling hide() manually is a MUST required by Variable.
        hide();
        if (_sampler) {
            _sampler->destroy();
            _sampler = NULL;
        }
        if (_series_sampler) {
            _series_sampler->destroy();
            _series_sampler = NULL;
        }
    }

    // Add a value.
    // Returns self reference for chaining.
    Reducer& operator<<(typename butil::add_cr_non_integral<T>::type value);

    // Get reduced value.
    // Notice that this function walks through threads that ever add values
    // into this reducer. You should avoid calling it frequently.
    T get_value() const {
        CHECK(!(butil::is_same<InvOp, detail::VoidOp>::value) || _sampler == NULL)
            << "You should not call Reducer<" << butil::class_name_str<T>()
            << ", " << butil::class_name_str<Op>() << ">::get_value() when a"
            << " Window<> is used because the operator does not have inverse.";
        return _combiner->combine_agents();
    }


    // Reset the reduced value to T().
    // Returns the reduced value before reset.
    T reset() { return _combiner->reset_all_agents(); }

    void describe(std::ostream& os, bool quote_string) const override {
        if (butil::is_same<T, std::string>::value && quote_string) {
            os << '"' << get_value() << '"';
        } else {
            os << get_value();
        }
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const override { *value = get_value(); }
#endif

    // True if this reducer is constructed successfully.
    bool valid() const { return _combiner->valid(); }

    // Get instance of Op.
    const Op& op() const { return _combiner->op(); }
    const InvOp& inv_op() const { return _inv_op; }
    
    sampler_type* get_sampler() {
        if (NULL == _sampler) {
            _sampler = new sampler_type(this);
            _sampler->schedule();
        }
        return _sampler;
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
            !butil::is_same<InvOp, detail::VoidOp>::value &&
            !butil::is_same<T, std::string>::value &&
            FLAGS_save_series) {
            _series_sampler = new SeriesSampler(this, _combiner->op());
            _series_sampler->schedule();
        }
        return rc;
    }

private:
    shared_combiner_type _combiner;
    sampler_type* _sampler;
    SeriesSampler* _series_sampler;
    InvOp _inv_op;
};

template <typename T, typename Op, typename InvOp>
inline Reducer<T, Op, InvOp>& Reducer<T, Op, InvOp>::operator<<(
    typename butil::add_cr_non_integral<T>::type value) {
    // It's wait-free for most time
    agent_type* agent = _combiner->get_or_create_tls_agent();
    if (__builtin_expect(!agent, 0)) {
        LOG(FATAL) << "Fail to create agent";
        return *this;
    }
    agent->element.modify(_combiner->op(), value);
    return *this;
}

// =================== Common reducers ===================

// bvar::Adder<int> sum;
// sum << 1 << 2 << 3 << 4;
// LOG(INFO) << sum.get_value(); // 10
// Commonly used functors
namespace detail {
template <typename Tp>
struct AddTo {
    void operator()(Tp & lhs,
                    typename butil::add_cr_non_integral<Tp>::type rhs) const
    { lhs += rhs; }
};
template <typename Tp>
struct MinusFrom {
    void operator()(Tp & lhs,
                    typename butil::add_cr_non_integral<Tp>::type rhs) const
    { lhs -= rhs; }
};
} // namespace detail

template <typename T, typename = void>
class Adder : public Reducer<T, detail::AddTo<T>, detail::MinusFrom<T> > {
public:
    typedef Reducer<T, detail::AddTo<T>, detail::MinusFrom<T> > Base;
    typedef T value_type;
    typedef typename Base::sampler_type sampler_type;

    Adder() : Base() {}
    Adder(const butil::StringPiece& name) : Base() {
        this->expose(name);
    }
    Adder(const butil::StringPiece& prefix,
          const butil::StringPiece& name) : Base() {
        this->expose_as(prefix, name);
    }
    ~Adder() override { Variable::hide(); }
};

#if WITH_BABYLON_COUNTER
// Numerical types supported by babylon counter.
template <typename T>
class Adder<T, std::enable_if<std::is_constructible<babylon::GenericsConcurrentAdder<T>>::value>>
    : public detail::BabylonVariable<T, babylon::GenericsConcurrentAdder<T>,
                                     detail::AddTo<T>, detail::MinusFrom<T>> {
public:
    typedef  T value_type;
private:
    typedef detail::BabylonVariable<T, babylon::GenericsConcurrentAdder<T>,
                                    detail::AddTo<value_type>, detail::MinusFrom<value_type>> Base;
public:
    typedef detail::AddTo<value_type> Op;
    typedef detail::MinusFrom<value_type> InvOp;
    typedef typename Base::sampler_type sampler_type;

    COMMON_VARIABLE_CONSTRUCTOR(Adder);
};
#endif // WITH_BABYLON_COUNTER

// bvar::Maxer<int> max_value;
// max_value << 1 << 2 << 3 << 4;
// LOG(INFO) << max_value.get_value(); // 4
namespace detail {
template <typename Tp> 
struct MaxTo {
    void operator()(Tp & lhs, 
                    typename butil::add_cr_non_integral<Tp>::type rhs) const {
        // Use operator< as well.
        if (lhs < rhs) {
            lhs = rhs;
        }
    }
};

class LatencyRecorderBase;
} // namespace detail

template <typename T, typename = void>
class Maxer : public Reducer<T, detail::MaxTo<T> > {
public:
    typedef Reducer<T, detail::MaxTo<T> > Base;
    typedef T value_type;
    typedef typename Base::sampler_type sampler_type;

    Maxer() : Base(std::numeric_limits<T>::min()) {}
    Maxer(const butil::StringPiece& name)
        : Base(std::numeric_limits<T>::min()) {
        this->expose(name);
    }
    Maxer(const butil::StringPiece& prefix, const butil::StringPiece& name)
        : Base(std::numeric_limits<T>::min()) {
        this->expose_as(prefix, name);
    }
    ~Maxer() override { Variable::hide(); }

private:
    friend class detail::LatencyRecorderBase;
    // The following private funcition a now used in LatencyRecorder,
    // it's dangerous so we don't make them public
    explicit Maxer(T default_value) : Base(default_value) {
    }
    Maxer(T default_value, const butil::StringPiece& prefix,
          const butil::StringPiece& name) 
        : Base(default_value) {
        this->expose_as(prefix, name);
    }
    Maxer(T default_value, const butil::StringPiece& name) : Base(default_value) {
        this->expose(name);
    }
};

#if WITH_BABYLON_COUNTER
namespace detail {
template <typename T>
class ConcurrentMaxer : public babylon::GenericsConcurrentMaxer<T> {
    typedef babylon::GenericsConcurrentMaxer<T> Base;
public:
    ConcurrentMaxer() = default;
    ConcurrentMaxer(T default_value) : _default_value(default_value) {}

    T value() const {
        T result;
        if (!Base::value(result)) {
            return _default_value;
        }
        return std::max(result, _default_value);
    }
private:
    T _default_value{0};
};
} // namespace detail

// Numerical types supported by babylon counter.
template <typename T>
class Maxer<T, std::enable_if<std::is_constructible<detail::ConcurrentMaxer<T>>::value>>
    : public detail::BabylonVariable<T, detail::ConcurrentMaxer<T>,
                                     detail::MaxTo<T>, detail::VoidOp> {
public:
    typedef T value_type;
private:
    typedef detail::BabylonVariable<T, detail::ConcurrentMaxer<T>,
                                    detail::MaxTo<value_type>, detail::VoidOp> Base;
public:
    typedef detail::MaxTo<value_type> Op;
    typedef detail::VoidOp InvOp;
    typedef typename Base::sampler_type sampler_type;

    COMMON_VARIABLE_CONSTRUCTOR(Maxer);

private:
friend class detail::LatencyRecorderBase;

    Maxer(T default_value) : Base(default_value) {}
    Maxer(T default_value, const butil::StringPiece& prefix, const butil::StringPiece& name)
        : Base(default_value) {
        Variable::expose_as(prefix, name);
    }
    Maxer(T default_value, const butil::StringPiece& name)
        : Base(default_value) {
        Variable::expose(name);
    }
};
#endif // WITH_BABYLON_COUNTER

// bvar::Miner<int> min_value;
// min_value << 1 << 2 << 3 << 4;
// LOG(INFO) << min_value.get_value(); // 1
namespace detail {
template <typename Tp>
struct MinTo {
    void operator()(Tp & lhs,
                    typename butil::add_cr_non_integral<Tp>::type rhs) const {
        if (rhs < lhs) {
            lhs = rhs;
        }
    }
};
}  // namespace detail

template <typename T, typename = void>
class Miner : public Reducer<T, detail::MinTo<T> > {
public:
    typedef Reducer<T, detail::MinTo<T> > Base;
    typedef T value_type;
    typedef typename Base::sampler_type sampler_type;

    Miner() : Base(std::numeric_limits<T>::max()) {}
    Miner(const butil::StringPiece& name)
        : Base(std::numeric_limits<T>::max()) {
        this->expose(name);
    }
    Miner(const butil::StringPiece& prefix, const butil::StringPiece& name)
        : Base(std::numeric_limits<T>::max()) {
        this->expose_as(prefix, name);
    }
    ~Miner() override { Variable::hide(); }
};

#if WITH_BABYLON_COUNTER
// Numerical types supported by babylon counter.
template <typename T>
class Miner<T, std::enable_if<std::is_constructible<babylon::GenericsConcurrentMiner<T>>::value>>
    : public detail::BabylonVariable<T, babylon::GenericsConcurrentMiner<T>,
                                     detail::MinTo<T>, detail::VoidOp> {
public:
    typedef T value_type;
private:
    typedef detail::BabylonVariable<value_type, babylon::GenericsConcurrentMiner<T>,
                                    detail::MinTo<value_type>, detail::VoidOp> Base;
public:
    typedef detail::MinTo<value_type> Op;
    typedef detail::VoidOp InvOp;
    typedef typename Base::sampler_type sampler_type;

    COMMON_VARIABLE_CONSTRUCTOR(Miner);
};
#endif // WITH_BABYLON_COUNTER

}  // namespace bvar

#endif  //BVAR_REDUCER_H
