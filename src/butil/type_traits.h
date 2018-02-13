// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_TYPE_TRAITS_H
#define BUTIL_TYPE_TRAITS_H

#include <cstddef>  // For size_t.
#include "butil/build_config.h"

#if defined(BUTIL_CXX11_ENABLED)
#include <type_traits>
#endif

namespace butil {

// integral_constant, defined in tr1, is a wrapper for an integer
// value. We don't really need this generality; we could get away
// with hardcoding the integer type to bool. We use the fully
// general integer_constant for compatibility with tr1.
template <typename T, T v>
struct integral_constant {
  static const T value = v;
  typedef T value_type;
  typedef integral_constant<T, v> type;
};

template <typename T, T v> const T integral_constant<T, v>::value;

typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;


template <typename T> struct is_integral;
template <typename T> struct is_floating_point;
template <typename T> struct is_pointer;
template <typename T> struct is_member_function_pointer;
template <typename T> struct is_enum;
template <typename T> struct is_void;
template <typename T> struct is_pod;
template <typename T> struct is_const;
template <typename T> struct is_array;
template <typename T> struct is_reference;
template <typename T> struct is_non_const_reference;
template <typename T, typename U> struct is_same;
template <typename T> struct remove_const;
template <typename T> struct remove_volatile;
template <typename T> struct remove_cv;
template <typename T> struct remove_reference;
template <typename T> struct remove_const_reference;
template <typename T> struct add_const;
template <typename T> struct add_volatile;
template <typename T> struct add_cv;
template <typename T> struct add_reference;
template <typename T> struct add_const_reference;
template <typename T> struct remove_pointer;
template <typename T> struct add_cr_non_integral;
template <typename From, typename To> struct is_convertible;
template <bool C, typename TrueType, typename FalseType> struct conditional;


// is_integral is false except for the built-in integer types.
template <typename T> struct is_integral : false_type { };
template<> struct is_integral<bool> : true_type { };
template<> struct is_integral<char> : true_type { };
template<> struct is_integral<unsigned char> : true_type { };
template<> struct is_integral<signed char> : true_type { };
#if defined(_MSC_VER)
// wchar_t is not by default a distinct type from unsigned short in
// Microsoft C.
// See http://msdn2.microsoft.com/en-us/library/dh8che7s(VS.80).aspx
template<> struct is_integral<__wchar_t> : true_type { };
#else
template<> struct is_integral<wchar_t> : true_type { };
#endif
template<> struct is_integral<short> : true_type { };
template<> struct is_integral<unsigned short> : true_type { };
template<> struct is_integral<int> : true_type { };
template<> struct is_integral<unsigned int> : true_type { };
template<> struct is_integral<long> : true_type { };
template<> struct is_integral<unsigned long> : true_type { };
template<> struct is_integral<long long> : true_type { };
template<> struct is_integral<unsigned long long> : true_type { };

// is_floating_point is false except for the built-in floating-point types.
template <typename T> struct is_floating_point : false_type { };
template<> struct is_floating_point<float> : true_type { };
template<> struct is_floating_point<double> : true_type { };
template<> struct is_floating_point<long double> : true_type { };

template <typename T> struct is_pointer : false_type {};
template <typename T> struct is_pointer<T*> : true_type {};

#if defined(BUTIL_CXX11_ENABLED)
template <class T> struct is_pod : std::is_pod<T> {};
#else
// We can't get is_pod right without compiler help, so fail conservatively.
// We will assume it's false except for arithmetic types, enumerations,
// pointers and cv-qualified versions thereof. Note that std::pair<T,U>
// is not a POD even if T and U are PODs.
template <class T> struct is_pod
: integral_constant<bool, (is_integral<T>::value ||
                           is_floating_point<T>::value ||
                           is_enum<T>::value ||
                           is_pointer<T>::value)> { };
template <class T> struct is_pod<const T> : is_pod<T> { };
template <class T> struct is_pod<volatile T> : is_pod<T> { };
template <class T> struct is_pod<const volatile T> : is_pod<T> { };
#endif

// Member function pointer detection up to four params. Add more as needed
// below. This is built-in to C++ 11, and we can remove this when we switch.
template <typename T>
struct is_member_function_pointer : false_type {};

template <typename R, typename Z>
struct is_member_function_pointer<R(Z::*)()> : true_type {};
template <typename R, typename Z>
struct is_member_function_pointer<R(Z::*)() const> : true_type {};

template <typename R, typename Z, typename A>
struct is_member_function_pointer<R(Z::*)(A)> : true_type {};
template <typename R, typename Z, typename A>
struct is_member_function_pointer<R(Z::*)(A) const> : true_type {};

template <typename R, typename Z, typename A, typename B>
struct is_member_function_pointer<R(Z::*)(A, B)> : true_type {};
template <typename R, typename Z, typename A, typename B>
struct is_member_function_pointer<R(Z::*)(A, B) const> : true_type {};

template <typename R, typename Z, typename A, typename B, typename C>
struct is_member_function_pointer<R(Z::*)(A, B, C)> : true_type {};
template <typename R, typename Z, typename A, typename B, typename C>
struct is_member_function_pointer<R(Z::*)(A, B, C) const> : true_type {};

template <typename R, typename Z, typename A, typename B, typename C,
          typename D>
struct is_member_function_pointer<R(Z::*)(A, B, C, D)> : true_type {};
template <typename R, typename Z, typename A, typename B, typename C,
          typename D>
struct is_member_function_pointer<R(Z::*)(A, B, C, D) const> : true_type {};

// Specified by TR1 [4.6] Relationships between types
template <typename T, typename U> struct is_same : public false_type {};
template <typename T> struct is_same<T,T> : true_type {};

template <typename> struct is_array : public false_type {};
template <typename T, size_t n> struct is_array<T[n]> : public true_type {};
template <typename T> struct is_array<T[]> : public true_type {};

template <typename T> struct is_non_const_reference : false_type {};
template <typename T> struct is_non_const_reference<T&> : true_type {};
template <typename T> struct is_non_const_reference<const T&> : false_type {};

template <typename T> struct is_const : false_type {};
template <typename T> struct is_const<const T> : true_type {};

template <typename T> struct is_void : false_type {};
template <> struct is_void<void> : true_type {};

namespace internal {

// Types YesType and NoType are guaranteed such that sizeof(YesType) <
// sizeof(NoType).
typedef char YesType;

struct NoType {
  YesType dummy[2];
};

// This class is an implementation detail for is_convertible, and you
// don't need to know how it works to use is_convertible. For those
// who care: we declare two different functions, one whose argument is
// of type To and one with a variadic argument list. We give them
// return types of different size, so we can use sizeof to trick the
// compiler into telling us which function it would have chosen if we
// had called it with an argument of type From.  See Alexandrescu's
// _Modern C++ Design_ for more details on this sort of trick.

struct ConvertHelper {
  template <typename To>
  static YesType Test(To);

  template <typename To>
  static NoType Test(...);

  template <typename From>
  static From& Create();
};

// Used to determine if a type is a struct/union/class. Inspired by Boost's
// is_class type_trait implementation.
struct IsClassHelper {
  template <typename C>
  static YesType Test(void(C::*)(void));

  template <typename C>
  static NoType Test(...);
};

// For implementing is_empty
#if defined(COMPILER_MSVC)
#pragma warning(push)
#pragma warning(disable:4624) // destructor could not be generated
#endif

template <typename T>
struct EmptyHelper1 : public T {
    EmptyHelper1();  // hh compiler bug workaround
    int i[256];
private:
    // suppress compiler warnings:
    EmptyHelper1(const EmptyHelper1&);
    EmptyHelper1& operator=(const EmptyHelper1&);
};

#if defined(COMPILER_MSVC)
#pragma warning(pop)
#endif

struct EmptyHelper2 {
    int i[256];
};

}  // namespace internal


// Inherits from true_type if From is convertible to To, false_type otherwise.
//
// Note that if the type is convertible, this will be a true_type REGARDLESS
// of whether or not the conversion would emit a warning.
template <typename From, typename To>
struct is_convertible
    : integral_constant<bool,
                        sizeof(internal::ConvertHelper::Test<To>(
                                   internal::ConvertHelper::Create<From>())) ==
                        sizeof(internal::YesType)> {
};

template <typename T>
struct is_class
    : integral_constant<bool,
                        sizeof(internal::IsClassHelper::Test<T>(0)) ==
                            sizeof(internal::YesType)> {
};

// True if T is an empty class/struct
// NOTE: not work for union
template <typename T>
struct is_empty : integral_constant<bool, is_class<T>::value &&
    sizeof(internal::EmptyHelper1<T>) == sizeof(internal::EmptyHelper2)> {};

template <bool B, typename T = void>
struct enable_if {};

template <typename T>
struct enable_if<true, T> { typedef T type; };

// Select type by C.
template <bool C, typename TrueType, typename FalseType>
struct conditional {
    typedef TrueType type;
};
template <typename TrueType, typename FalseType>
struct conditional<false, TrueType, FalseType> {
    typedef FalseType type;
};

// Specified by TR1 [4.7.1]
template <typename _Tp> struct add_const { typedef _Tp const  type; };
template <typename _Tp> struct add_volatile { typedef _Tp volatile  type; };
template <typename _Tp> struct add_cv {
    typedef typename add_const<typename add_volatile<_Tp>::type>::type type;
};
template <typename T> struct remove_const { typedef T type; };
template <typename T> struct remove_const<T const> { typedef T type; };
template <typename T> struct remove_volatile { typedef T type; };
template <typename T> struct remove_volatile<T volatile> { typedef T type; };
template <typename T> struct remove_cv {
    typedef typename remove_const<typename remove_volatile<T>::type>::type type;
};

// Specified by TR1 [4.7.2] Reference modifications.
template <typename T> struct remove_reference { typedef T type; };
template <typename T> struct remove_reference<T&> { typedef T type; };

template <typename T> struct add_reference { typedef T& type; };
template <typename T> struct add_reference<T&> { typedef T& type; };
// Specializations for void which can't be referenced.
template <> struct add_reference<void> { typedef void type; };
template <> struct add_reference<void const> { typedef void const type; };
template <> struct add_reference<void volatile> { typedef void volatile type; };
template <> struct add_reference<void const volatile> { typedef void const volatile type; };

// Shortcut for adding/removing const&
template <typename T> struct add_const_reference { 
    typedef typename add_reference<typename add_const<T>::type>::type type;
};
template <typename T> struct remove_const_reference { 
    typedef typename remove_const<typename remove_reference<T>::type>::type type;
};

// Add const& for non-integral types.
// add_cr_non_integral<int>::type      -> int
// add_cr_non_integral<FooClass>::type -> const FooClass&
template <typename T> struct add_cr_non_integral {
    typedef typename conditional<is_integral<T>::value, T, 
            typename add_reference<typename add_const<T>::type>::type>::type type;
};

// Specified by TR1 [4.7.4] Pointer modifications.
template <typename T> struct remove_pointer { typedef T type; };
template <typename T> struct remove_pointer<T*> { typedef T type; };
template <typename T> struct remove_pointer<T* const> { typedef T type; };
template <typename T> struct remove_pointer<T* volatile> { typedef T type; };
template <typename T> struct remove_pointer<T* const volatile> {
    typedef T type; 
};

// is_reference is false except for reference types.
template<typename T> struct is_reference : false_type {};
template<typename T> struct is_reference<T&> : true_type {};

namespace internal {
// is_convertible chokes if the first argument is an array. That's why
// we use add_reference here.
template <bool NotUnum, typename T> struct is_enum_impl
    : is_convertible<typename add_reference<T>::type, int> { };

template <typename T> struct is_enum_impl<true, T> : false_type { };

}

template <typename T> struct is_enum
    : internal::is_enum_impl<
                is_same<T, void>::value ||
                    is_integral<T>::value ||
                    is_floating_point<T>::value ||
                    is_reference<T>::value ||
                    is_class<T>::value, T> { };

template <typename T> struct is_enum<const T> : is_enum<T> { };
template <typename T> struct is_enum<volatile T> : is_enum<T> { };
template <typename T> struct is_enum<const volatile T> : is_enum<T> { };

}  // namespace butil

#endif  // BUTIL_TYPE_TRAITS_H
