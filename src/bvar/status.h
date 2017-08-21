// Copyright (c) 2014 Baidu.com, Inc. All Rights Reserved
// Author Zhangyi Chen (chenzhangyi01@baidu.com)
// Date 2014/09/22 11:57:43

#ifndef  BVAR_STATUS_H
#define  BVAR_STATUS_H

#include <string>                       // std::string
#include "base/atomicops.h"
#include "base/type_traits.h"
#include "base/string_printf.h"
#include "base/synchronization/lock.h"
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
    Status(const base::StringPiece& name, const T& value) : _value(value) {
        this->expose(name);
    }
    Status(const base::StringPiece& prefix,
           const base::StringPiece& name, const T& value) : _value(value) {
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
        base::AutoLock guard(_lock);
        *value = _value;
    }
#endif

    T get_value() const {
        base::AutoLock guard(_lock);
        const T res = _value;
        return res;
    }

    void set_value(const T& value) {
        base::AutoLock guard(_lock);
        _value = value;
    }

private:
    T _value;
    // We use lock rather than base::atomic for generic values because
    // base::atomic requires the type to be memcpy-able (POD basically)
    mutable base::Lock _lock;
};

template <typename T>
class Status<T, typename base::enable_if<detail::is_atomical<T>::value>::type>
    : public Variable {
public:
    Status() {}
    Status(const T& value) : _value(value) { }
    Status(const base::StringPiece& name, const T& value) : _value(value) {
        this->expose(name);
    }
    Status(const base::StringPiece& prefix,
           const base::StringPiece& name, const T& value) : _value(value) {
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
        return _value.load(base::memory_order_relaxed);
    }
    
    void set_value(const T& value) {
        _value.store(value, base::memory_order_relaxed);
    }

private:
    base::atomic<T> _value;
};

// Specialize for std::string, adding a printf-style set_value().
template <>
class Status<std::string, void> : public Variable {
public:
    Status() {}
    Status(const base::StringPiece& name, const char* fmt, ...) {
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            base::string_vprintf(&_value, fmt, ap);
            va_end(ap);
        }
        expose(name);
    }
    Status(const base::StringPiece& prefix,
           const base::StringPiece& name, const char* fmt, ...) {
        if (fmt) {
            va_list ap;
            va_start(ap, fmt);
            base::string_vprintf(&_value, fmt, ap);
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
        base::AutoLock guard(_lock);
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
            base::AutoLock guard(_lock);
            base::string_vprintf(&_value, fmt, ap);
        }
        va_end(ap);
    }

    void set_value(const std::string& s) {
        base::AutoLock guard(_lock);
        _value = s;
    }

private:
    std::string _value;
    mutable base::Lock _lock;
};

}  // namespace bvar

#endif  //BVAR_STATUS_H
