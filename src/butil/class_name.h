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

// Date: Mon. Nov 7 14:47:36 CST 2011

// Get name of a class. For example, class_name<T>() returns the name of T
// (with namespace prefixes). This is useful in template classes.

#ifndef BUTIL_CLASS_NAME_H
#define BUTIL_CLASS_NAME_H

#include <typeinfo>
#include <string>                                // std::string

namespace butil {

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
    return demangle(typeid(obj).name());
}

}  // namespace butil

#endif  // BUTIL_CLASS_NAME_H
