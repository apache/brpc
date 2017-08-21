// Copyright (c) 2011 Baidu.com, Inc. All Rights Reserved
//
// Get name of a class. For example, class_name<T>() returns the name of T
// (with namespace prefixes). This is useful in template classes.
//
// Author: Ge,Jun (gejun@baidu.com)
// Date: Mon. Nov 7 14:47:36 CST 2011

#ifndef BRPC_BASE_CLASS_NAME_H
#define BRPC_BASE_CLASS_NAME_H

#include <typeinfo>
#include <string>                                // std::string

namespace base {

std::string demangle(const char* name);

namespace detail {
template <typename T> struct ClassNameHelper { static std::string name; };
template <typename T> std::string ClassNameHelper<T>::name = demangle(typeid(T).name());
}

// Get name of class |T|, in std::string.
template <typename T> const std::string& class_name_str() {
    // We don't use static-variable-inside-function because before C++11
    // local static variable is not guaranteed to be thread-safe.
    return detail::ClassNameHelper<T>::name;
}

// Get name of class |T|, in const char*.
// Address of returned name never changes.
template <typename T> const char* class_name() {
    return class_name_str<T>().c_str();
}

// Get typename of |obj|, in std::string
template <typename T> std::string class_name_str(T const& obj) {
    extern std::string demangle(const char* name);
    return demangle(typeid(obj).name());
}

}  // namespace base

#endif  // BRPC_BASE_CLASS_NAME_H
