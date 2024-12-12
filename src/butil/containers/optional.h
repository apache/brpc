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

#ifndef BUTIL_OPTIONAL_H
#define BUTIL_OPTIONAL_H

#include "butil/type_traits.h"
#include "butil/memory/manual_constructor.h"

// The `optional` for managing an optional contained value,
// i.e. a value that may or may not be present, is a C++11
// compatible version of the C++17 `std::optional` abstraction.
// After C++17, `optional` is an alias for `std::optional`.

#if __cplusplus >= 201703L

#include <optional>

namespace butil {
using std::in_place_t;
using std::in_place;
using std::nullopt_t;
using std::nullopt;
using std::bad_optional_access;
using std::optional;
using std::make_optional;
} // namespace butil
#else
namespace butil {

// Tag type used to specify in-place construction of `optional'.
struct in_place_t {};

// Instance used for in-place construction of `optional',
const in_place_t in_place;

namespace internal {
    // Tag type used as a constructor parameter type for `nullopt_t'.
    struct optional_forbidden_t {};
}

// Used to indicate an optional object that does not contain a value.
struct nullopt_t {
    // It must not be default-constructible to avoid ambiguity for `opt = {}'.
    explicit nullopt_t(internal::optional_forbidden_t) noexcept {}
};

// Instance to use for the construction of `optional`.
const nullopt_t nullopt(internal::optional_forbidden_t{});

// Thrown by `optional::value()' when accessing an optional object
// that does not contain a value.
class bad_optional_access : public std::exception {
public:
    bad_optional_access() = default;
    ~bad_optional_access() override = default;
    const char* what() const noexcept override {
        return "optional has no value";
    }
};

template <typename T>
class optional;

namespace internal {

// Whether `T' is constructible or convertible from `optional<U>'.
template <typename T, typename U>
struct is_constructible_from_optional : integral_constant<
    bool, std::is_constructible<T, optional<U>&>::value ||
          std::is_constructible<T, optional<U>&&>::value ||
          std::is_constructible<T, const optional<U>&>::value ||
          std::is_constructible<T, const optional<U>&&>::value> {};

// Whether `T' is constructible or convertible from `butil::optional<U>'.
template <typename T, typename U>
struct is_convertible_from_optional
    : integral_constant<bool,
        std::is_convertible<optional<U>&, T>::value ||
        std::is_convertible<optional<U>&&, T>::value ||
        std::is_convertible<const optional<U>&, T>::value ||
        std::is_convertible<const optional<U>&&, T>::value> {};

// Whether `T' is constructible or convertible or assignable from `optional<U>'.
template <typename T, typename U>
struct is_assignable_from_optional
    : integral_constant<bool,
        std::is_assignable<T&, optional<U>&>::value ||
        std::is_assignable<T&, optional<U>&&>::value ||
        std::is_assignable<T&, const optional<U>&>::value ||
        std::is_assignable<T&, const optional<U>&&>::value> {};

// Whether `T' is constructible or convertible from `optional<U>'.
template <typename T, typename U>
struct is_constructible_convertible_from_optional
    : integral_constant<bool,
        is_constructible_from_optional<T, U>::value ||
        is_convertible_from_optional<T, U>::value> {};

// Whether `T' is constructible or convertible or assignable from `optional<U>'.
template <typename T, typename U>
struct is_constructible_convertible_assignable_from_optional
    : integral_constant<bool,
        is_constructible_from_optional<T, U>::value ||
        is_convertible_from_optional<T, U>::value ||
        is_assignable_from_optional<T, U>::value> {};

} // namespace internal

template<typename T>
class optional {
public:
    typedef T value_type;

    static_assert(!is_same<typename remove_cvref<value_type>::type, in_place_t>::value,
                  "Instantiation of optional with in_place_t is ill-formed");
    static_assert(!is_same<typename remove_cvref<value_type>::type, nullopt_t>::value,
                  "Instantiation of optional with nullopt_t is ill-formed");
    static_assert(!is_reference<value_type>::value,
                  "Instantiation of optional with a reference type is ill-formed");
    static_assert(std::is_destructible<value_type>::value,
                  "Instantiation of optional with a non-destructible type is ill-formed");
    static_assert(!is_array<value_type>::value,
                  "Instantiation of optional with an array type is ill-formed");

    optional() : _engaged(false) {}

    optional(nullopt_t) : optional() {}

    optional(const optional& rhs) = default;

    optional(optional&& rhs) noexcept = default;

    template <typename U, typename std::enable_if<
        !std::is_same<T, U>::value &&
        std::is_constructible<T, const U&>::value &&
        !internal::is_constructible_convertible_from_optional<T, U>::value &&
        std::is_convertible<const U&, T>::value, bool>::type = false>
    optional(const optional<U>& rhs) : _engaged(rhs.has_value()) {
        if (_engaged) {
            _storage.Init(*rhs);
        }
    }

    template <typename U, typename std::enable_if<
        !std::is_same<T, U>::value &&
        std::is_constructible<T, const U&>::value &&
        !internal::is_constructible_convertible_from_optional<T, U>::value &&
        !std::is_convertible<const U&, T>::value, bool>::type = false>
    explicit optional(const optional<U>& rhs) : _engaged(rhs.has_value()) {
        if (_engaged) {
            _storage.Init(*rhs);
        }
    }

    template <typename U, typename std::enable_if<
        !std::is_same<T, U>::value &&
        std::is_constructible<T, U&&>::value &&
        !internal::is_constructible_convertible_from_optional<T, U>::value &&
        std::is_convertible<U&&, T>::value, bool>::type = false>
    optional(optional<U>&& rhs) : _engaged(rhs.has_value()) {
        if (_engaged) {
            _storage.Init(std::move(*rhs));
            rhs.reset();
        }
    }

    template <typename U, typename std::enable_if<
        !std::is_same<T, U>::value &&
        std::is_constructible<T, U&&>::value &&
        !internal::is_constructible_convertible_from_optional<T, U>::value &&
        !std::is_convertible<U&&, T>::value, bool>::type = false>
    explicit optional(optional<U>&& rhs) : _engaged(rhs.has_value()) {
        if (_engaged) {
            _storage.Init(std::move(*rhs));
            rhs.reset();
        }
    }

    optional(const T& value) : _engaged(true) {
        _storage.Init(value);
    }

    optional(T&& value) : _engaged(true) {
        _storage.Init(std::move(value));
    }

    template <typename... Args,
              std::enable_if<std::is_constructible<T, Args&&...>::value>* = nullptr>
    explicit optional(const in_place_t, Args&&... args) : _engaged(true) {
        _storage.Init(std::forward<Args>(args)...);
    }

    template <typename U, typename... Args, typename std::enable_if<
        std::is_constructible<T, std::initializer_list<U>&, Args&&...>::value>::type>
    optional(in_place_t, std::initializer_list<U> il, Args&&... args)
        : _engaged(true) {
        _storage.Init(il, std::forward<Args>(args)...);
    }

    template <typename U = T, typename std::enable_if<
        !std::is_same<in_place_t, typename std::decay<U>::type>::value &&
        !std::is_same<optional<T>, typename std::decay<U>::type>::value &&
        std::is_constructible<T, U&&>::value &&
        std::is_convertible<U&&, T>::value, bool>::type = false>
    optional(U&& v) : _engaged(true) {
        _storage.Init(std::forward<U>(v));
    }

    template <typename U = T, typename std::enable_if<
        !std::is_same<in_place_t, typename std::decay<U>::type>::value &&
        !std::is_same<optional<T>, typename std::decay<U>::type>::value &&
        std::is_constructible<T, U&&>::value &&
        !std::is_convertible<U&&, T>::value, bool>::type = false>
    explicit optional(U&& v) : _engaged(true) {
        _storage.Init(std::forward<U>(v));
    }

    ~optional() {
        reset();
    }

    optional& operator=(nullopt_t) noexcept {
        reset();
        return *this;
    }

    optional& operator=(const optional& rhs) = default;

    optional& operator=(optional&& rhs) = default;

    // Value assignment operators
    template <typename U = T, typename = typename std::enable_if<
        !std::is_same<optional<T>, typename std::decay<U>::type>::value &&
        !std::is_same<optional<T>, typename remove_cvref<U>::type>::value &&
        std::is_constructible<T, U>::value && std::is_assignable<T&, U>::value &&
        (!std::is_scalar<T>::value || !std::is_same<T, typename std::decay<U>::type>::value)>::type>
    optional& operator=(U&& v) {
        reset();
        _storage.Init(std::forward<U>(v));
        _engaged = true;
        return *this;
    }

    template <typename U, typename = typename std::enable_if<
        !std::is_same<T, U>::value &&
        !internal::is_constructible_convertible_assignable_from_optional<T, U>::value &&
        std::is_constructible<T, const U&>::value &&
        std::is_assignable<T&, const U&>::value>::type>
    optional& operator=(const optional<U>& rhs) {
        if (rhs) {
            operator=(*rhs);
        } else {
            reset();
        }
        return *this;
    }

    template <typename U, typename = typename std::enable_if<
        !std::is_same<T, U>::value &&
        !internal::is_constructible_convertible_assignable_from_optional<T, U>::value &&
        std::is_constructible<T, U>::value &&
        std::is_assignable<T&, U>::value>::type>
    optional& operator=(optional<U>&& rhs) {
        if (rhs) {
            operator=(std::move(*rhs));
        } else {
            reset();
        }
        return *this;
    }

    // Accesses the contained value.
    T& operator*() {
        if (!_engaged) {
            throw bad_optional_access();
        }
        return *_storage;
    }
    const T& operator*() const {
        if (!_engaged) {
            throw bad_optional_access();
        }
        return *_storage;
    }
    T* operator->() {
        return _storage.get();
    }
    const T* operator->() const {
        return _storage.get();
    }

    explicit operator bool() const { return _engaged; }

    bool has_value() const { return _engaged; }

    // Returns the contained value if the `optional` is not empty,
    // otherwise throws `bad_optional_access`.
    const T& value() const & {
        if (!_engaged) {
            throw bad_optional_access();
        }

        return *_storage;
    }
    T& value() & {
        if (!_engaged) {
            throw bad_optional_access();
        }

        return *_storage;
    }
    T&& value() && {
        if (!_engaged) {
            throw bad_optional_access();
        }

        return std::move(*_storage);
    }
    const T&& value() const && {
        if (!_engaged) {
            throw bad_optional_access();
        }

        return std::move(*_storage);
    }

    // Returns the contained value if the `optional` is not empty,
    // otherwise returns default `v'.
    template <typename U>
    constexpr T value_or(U&& v) const& {
        static_assert(std::is_copy_constructible<value_type>::value,
                      "T can not be copy constructible");
        static_assert(std::is_convertible<U&&, value_type>::value,
                      "U can not be convertible to T");

        return static_cast<bool>(*this) ? **this : static_cast<T>(std::forward<U>(v));
    }
    template <typename U>
    T value_or(U&& v) && {
        static_assert(std::is_move_constructible<value_type>::value,
                      "T can not be move constructible");
        static_assert(std::is_convertible<U&&, value_type>::value,
                      "U can not be convertible to T");

        return static_cast<bool>(*this) ?
            std::move(**this) : static_cast<T>(std::forward<U>(v));
    }

    void swap(optional& rhs) noexcept {
        if (_engaged && rhs._engaged) {
            std::swap(**this, *rhs);
        } else if (_engaged) {
            rhs = std::move(*this);
            reset();
        } else if (rhs._engaged) {
            *this = std::move(rhs);
            rhs.reset();
        }
    }

    // Destroys any contained value if the `optional` is not empty.
    void reset() {
        if (_engaged) {
            _storage.Destroy();
            _engaged = false;
        }
    }

    // Constructs the contained value in-place.
    // Return a reference to the new contained value.
    template <typename... Args>
    T& emplace(Args&&... args) {
        static_assert(std::is_constructible<T, Args&&...>::value,
                      "T can not be constructible with these arguments");
        reset();
        _storage.Init(std::forward<Args>(args)...);
        _engaged = true;
        return *_storage;
    }
    template <typename U, typename... Args>
    T& emplace(std::initializer_list<U> il, Args&&... args) {
        static_assert(std::is_constructible<T, std::initializer_list<U>&, Args&&...>::value,
                      "T can not be constructible with these arguments");

        return emplace(il, std::forward<Args>(args)...);
    }

private:
    bool _engaged;
    ManualConstructor<T> _storage;
};

template <typename T>
void swap(optional<T>& a, optional<T>& b) noexcept { a.swap(b); }

// Creates a non-empty 'optional<T>`.
template <typename T>
optional<typename std::decay<T>::type> make_optional(T&& v) {
    return optional<typename std::decay<T>::type>(std::forward<T>(v));
}

template <typename T, typename... Args>
optional<T> make_optional(Args&&... args) {
    return optional<T>(in_place, std::forward<Args>(args)...);
}

template <typename T, typename U, typename... Args>
optional<T> make_optional(std::initializer_list<U> il, Args&&... args) {
    return optional<T>(in_place, il, std::forward<Args>(args)...);
}

// Compares optional objects.
// Supports comparisons between 'optional<T>` and 'optional<U>`,
// between 'optional<T>` and `U', and between 'optional<T>` and `nullopt'.
// Empty optionals are considered equal to each other and less than non-empty optionals.
template <typename T, typename U>
bool operator==(const optional<T>& x, const optional<U>& y) {
    return static_cast<bool>(x) != static_cast<bool>(y)
        ? false : static_cast<bool>(x) == false ? true : *x == *y;
}

template <typename T, typename U>
bool operator!=(const optional<T>& x, const optional<U>& y) {
    return !(x == y);
}

template <typename T, typename U>
bool operator<=(const optional<T>& x, const optional<U>& y) {
    return !x ? true : !y ? false : *x <= *y;
}

template <typename T, typename U>
bool operator>=(const optional<T>& x, const optional<U>& y) {
    return !y ? true : !x ? false : *x >= *y;
}

template <typename T, typename U>
bool operator<(const optional<T>& x, const optional<U>& y) {
    return !(x >= y);
}

template <typename T, typename U>
bool operator>(const optional<T>& x, const optional<U>& y) {
    return !(x <= y);
}

template <typename T>
bool operator==(const optional<T>& x, nullopt_t) { return !x; }

template <typename T>
bool operator==(nullopt_t, const optional<T>& x) { return !x; }

template <typename T>
bool operator!=(const optional<T>& x, nullopt_t) { return static_cast<bool>(x); }

template <typename T>
bool operator!=(nullopt_t, const optional<T>& x) { return static_cast<bool>(x);}

template <typename T>
bool operator<(const optional<T>&, nullopt_t) { return false; }

template <typename T>
bool operator<(nullopt_t, const optional<T>& x) { return static_cast<bool>(x); }

template <typename T>
bool operator<=(const optional<T>& x, nullopt_t) { return !x; }

template <typename T>
bool operator<=(nullopt_t, const optional<T>&) { return true; }

template <typename T>
bool operator>(const optional<T>& x, nullopt_t) { return static_cast<bool>(x); }

template <typename T>
bool operator>(nullopt_t, const optional<T>&) { return false; }

template <typename T>
bool operator>=(const optional<T>&, nullopt_t) { return true; }
template <typename T>
bool operator>=(nullopt_t, const optional<T>& x) { return !x; }

template <typename T, typename U>
bool operator==(const optional<T>& x, const U& v) {
    return x ? *x == v : false;
}
template <typename T, typename U>
bool operator==(const U& v, const optional<T>& x) {
    return x == v;
}
template <typename T, typename U>
bool operator!=(const optional<T>& x, const U& v) {
    return x ? *x != v : true;
}
template <typename T, typename U>
bool operator!=(const U& v, const optional<T>& x) {
    return x != v;
}

template <typename T, typename U>
bool operator<=(const optional<T>& x, const U& v) {
    return x ? *x <= v : true;
}
template <typename T, typename U>
bool operator<=(const U& v, const optional<T>& x) {
    return x ? v <= *x : false;
}
template <typename T, typename U>
bool operator<(const optional<T>& x, const U& v) {
    return !(x >= v);
}
template <typename T, typename U>
bool operator<(const U& v, const optional<T>& x) {
    return !(v >= x);
}

template <typename T, typename U>
bool operator>=(const optional<T>& x, const U& v) {
    return x ? *x >= v : false;
}
template <typename T, typename U>
bool operator>=(const U& v, const optional<T>& x) {
    return x ? v >= *x : true;
}

template <typename T, typename U>
bool operator>(const optional<T>& x, const U& v) {
    return !(x <= v);
}
template <typename T, typename U>
bool operator>(const U& v, const optional<T>& x) {
    return !(v <= x);
}

} // namespace butil

namespace std {
template <typename T>
struct hash<butil::optional<T>> {
    std::size_t operator()(const butil::optional<T>& opt) const {
        return hash<T>()(opt.value_or(T()));
    }
};

}  // namespace std

#endif // __cplusplus >= 201703L

#endif // BUTIL_OPTIONAL_H
