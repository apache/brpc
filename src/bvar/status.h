// Copyright (c) 2014 Baidu, Inc.
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

// Author Zhangyi Chen (chenzhangyi01@baidu.com)
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

    // Implement Variable::describe() and Variable::get_value().
    void describe(std::ostream& os, bool /*quote_string*/) const {
        os << get_value();
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const {
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
    Status() {}
    Status(const T& value) : _value(value) { }
    Status(const butil::StringPiece& name, const T& value) : _value(value) {
        this->expose(name);
    }
    Status(const butil::StringPiece& prefix,
           const butil::StringPiece& name, const T& value) : _value(value) {
        this->expose_as(prefix, name);
    }
    ~Status() { hide(); }

    // Implement Variable::describe() and Variable::get_value().
    void describe(std::ostream& os, bool /*quote_string*/) const {
        os << get_value();
    }
    
#ifdef BAIDU_INTERNAL
    void get_value(boost::any* value) const {
        *value = get_value();
    }
#endif
    
    T get_value() const {
        return _value.load(butil::memory_order_relaxed);
    }
    
    void set_value(const T& value) {
        _value.store(value, butil::memory_order_relaxed);
    }

private:
    butil::atomic<T> _value;
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

    void describe(std::ostream& os, bool quote_string) const {
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
    void get_value(boost::any* value) const {
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
