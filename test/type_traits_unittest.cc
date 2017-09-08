// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/type_traits.h"

#include "butil/basictypes.h"
#include <gtest/gtest.h>

namespace butil {
namespace {

struct AStruct {};
class AClass {};
union AUnion {};
enum AnEnum { AN_ENUM_APPLE, AN_ENUM_BANANA, AN_ENUM_CARROT };
struct BStruct {
    int x;
};
class BClass {
#if defined(__clang__)
    int ALLOW_UNUSED _x;
#else
    int _x;
#endif
};

class Parent {};
class Child : public Parent {};

// is_empty<Type>
COMPILE_ASSERT(is_empty<AStruct>::value, IsEmpty);
COMPILE_ASSERT(is_empty<AClass>::value, IsEmpty);
COMPILE_ASSERT(!is_empty<BStruct>::value, IsEmpty);
COMPILE_ASSERT(!is_empty<BClass>::value, IsEmpty);

// is_pointer<Type>
COMPILE_ASSERT(!is_pointer<int>::value, IsPointer);
COMPILE_ASSERT(!is_pointer<int&>::value, IsPointer);
COMPILE_ASSERT(is_pointer<int*>::value, IsPointer);
COMPILE_ASSERT(is_pointer<const int*>::value, IsPointer);

// is_array<Type>
COMPILE_ASSERT(!is_array<int>::value, IsArray);
COMPILE_ASSERT(!is_array<int*>::value, IsArray);
COMPILE_ASSERT(!is_array<int(*)[3]>::value, IsArray);
COMPILE_ASSERT(is_array<int[]>::value, IsArray);
COMPILE_ASSERT(is_array<const int[]>::value, IsArray);
COMPILE_ASSERT(is_array<int[3]>::value, IsArray);

// is_non_const_reference<Type>
COMPILE_ASSERT(!is_non_const_reference<int>::value, IsNonConstReference);
COMPILE_ASSERT(!is_non_const_reference<const int&>::value, IsNonConstReference);
COMPILE_ASSERT(is_non_const_reference<int&>::value, IsNonConstReference);

// is_convertible<From, To>

// Extra parens needed to make preprocessor macro parsing happy. Otherwise,
// it sees the equivalent of:
//
//     (is_convertible < Child), (Parent > ::value)
//
// Silly C++.
COMPILE_ASSERT( (is_convertible<Child, Parent>::value), IsConvertible);
COMPILE_ASSERT(!(is_convertible<Parent, Child>::value), IsConvertible);
COMPILE_ASSERT(!(is_convertible<Parent, AStruct>::value), IsConvertible);
COMPILE_ASSERT( (is_convertible<int, double>::value), IsConvertible);
COMPILE_ASSERT( (is_convertible<int*, void*>::value), IsConvertible);
COMPILE_ASSERT(!(is_convertible<void*, int*>::value), IsConvertible);

// Array types are an easy corner case.  Make sure to test that
// it does indeed compile.
COMPILE_ASSERT(!(is_convertible<int[10], double>::value), IsConvertible);
COMPILE_ASSERT(!(is_convertible<double, int[10]>::value), IsConvertible);
COMPILE_ASSERT( (is_convertible<int[10], int*>::value), IsConvertible);

// is_same<Type1, Type2>
COMPILE_ASSERT(!(is_same<Child, Parent>::value), IsSame);
COMPILE_ASSERT(!(is_same<Parent, Child>::value), IsSame);
COMPILE_ASSERT( (is_same<Parent, Parent>::value), IsSame);
COMPILE_ASSERT( (is_same<int*, int*>::value), IsSame);
COMPILE_ASSERT( (is_same<int, int>::value), IsSame);
COMPILE_ASSERT( (is_same<void, void>::value), IsSame);
COMPILE_ASSERT(!(is_same<int, double>::value), IsSame);


// is_class<Type>
COMPILE_ASSERT(is_class<AStruct>::value, IsClass);
COMPILE_ASSERT(is_class<AClass>::value, IsClass);
COMPILE_ASSERT(is_class<AUnion>::value, IsClass);
COMPILE_ASSERT(!is_class<AnEnum>::value, IsClass);
COMPILE_ASSERT(!is_class<int>::value, IsClass);
COMPILE_ASSERT(!is_class<char*>::value, IsClass);
COMPILE_ASSERT(!is_class<int&>::value, IsClass);
COMPILE_ASSERT(!is_class<char[3]>::value, IsClass);

// NOTE(gejun): Not work in gcc 3.4 yet.
#if !defined(__GNUC__) || __GNUC__ >= 4
COMPILE_ASSERT((is_enum<AnEnum>::value), IsEnum);
#endif
COMPILE_ASSERT(!(is_enum<AClass>::value), IsEnum);
COMPILE_ASSERT(!(is_enum<AStruct>::value), IsEnum);
COMPILE_ASSERT(!(is_enum<AUnion>::value), IsEnum);

COMPILE_ASSERT(!is_member_function_pointer<int>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<int*>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<void*>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<AStruct>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<AStruct*>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<int(*)(int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<int(*)(int, int)>::value,
               IsMemberFunctionPointer);

COMPILE_ASSERT(is_member_function_pointer<void (AStruct::*)()>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<void (AStruct::*)(int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<int (AStruct::*)(int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<int (AStruct::*)(int) const>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<int (AStruct::*)(int, int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<
                 int (AStruct::*)(int, int) const>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<
                 int (AStruct::*)(int, int, int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<
                 int (AStruct::*)(int, int, int) const>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<
                 int (AStruct::*)(int, int, int, int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(is_member_function_pointer<
                 int (AStruct::*)(int, int, int, int) const>::value,
               IsMemberFunctionPointer);

// False because we don't have a specialization for 5 params yet.
COMPILE_ASSERT(!is_member_function_pointer<
                 int (AStruct::*)(int, int, int, int, int)>::value,
               IsMemberFunctionPointer);
COMPILE_ASSERT(!is_member_function_pointer<
                 int (AStruct::*)(int, int, int, int, int) const>::value,
               IsMemberFunctionPointer);

// add_const
COMPILE_ASSERT((is_same<add_const<int>::type, const int>::value), AddConst);
COMPILE_ASSERT((is_same<add_const<long>::type, const long>::value), AddConst);
COMPILE_ASSERT((is_same<add_const<std::string>::type, const std::string>::value),
               AddConst);
COMPILE_ASSERT((is_same<add_const<const int>::type, const int>::value),
               AddConst);
COMPILE_ASSERT((is_same<add_const<const long>::type, const long>::value),
               AddConst);
COMPILE_ASSERT((is_same<add_const<const std::string>::type,
                const std::string>::value), AddConst);

// add_volatile
COMPILE_ASSERT((is_same<add_volatile<int>::type, volatile int>::value),
               AddVolatile);
COMPILE_ASSERT((is_same<add_volatile<long>::type, volatile long>::value),
               AddVolatile);
COMPILE_ASSERT((is_same<add_volatile<std::string>::type,
                volatile std::string>::value), AddVolatile);
COMPILE_ASSERT((is_same<add_volatile<volatile int>::type, volatile int>::value),
               AddVolatile);
COMPILE_ASSERT((is_same<add_volatile<volatile long>::type, volatile long>::value),
               AddVolatile);
COMPILE_ASSERT((is_same<add_volatile<volatile std::string>::type,
                volatile std::string>::value), AddVolatile);

// add_reference
COMPILE_ASSERT((is_same<add_reference<int>::type, int&>::value), AddReference);
COMPILE_ASSERT((is_same<add_reference<long>::type, long&>::value), AddReference);
COMPILE_ASSERT((is_same<add_reference<std::string>::type, std::string&>::value),
               AddReference);
COMPILE_ASSERT((is_same<add_reference<int&>::type, int&>::value),
               AddReference);
COMPILE_ASSERT((is_same<add_reference<long&>::type, long&>::value),
               AddReference);
COMPILE_ASSERT((is_same<add_reference<std::string&>::type,
                std::string&>::value), AddReference);
COMPILE_ASSERT((is_same<add_reference<const int&>::type, const int&>::value),
               AddReference);
COMPILE_ASSERT((is_same<add_reference<const long&>::type, const long&>::value),
               AddReference);
COMPILE_ASSERT((is_same<add_reference<const std::string&>::type,
                const std::string&>::value), AddReference);

// add_cr_non_integral
COMPILE_ASSERT((is_same<add_cr_non_integral<int>::type, int>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<long>::type, long>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<std::string>::type,
                const std::string&>::value), AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const int>::type, const int&>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const long>::type, const long&>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const std::string>::type,
                const std::string&>::value), AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const int&>::type, const int&>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const long&>::type, const long&>::value),
               AddCrNonIntegral);
COMPILE_ASSERT((is_same<add_cr_non_integral<const std::string&>::type,
                const std::string&>::value), AddCrNonIntegral);

// remove_const
COMPILE_ASSERT((is_same<remove_const<const int>::type, int>::value),
               RemoveConst);
COMPILE_ASSERT((is_same<remove_const<const long>::type, long>::value),
               RemoveConst);
COMPILE_ASSERT((is_same<remove_const<const std::string>::type,
                std::string>::value), RemoveConst);
COMPILE_ASSERT((is_same<remove_const<int>::type, int>::value), RemoveConst);
COMPILE_ASSERT((is_same<remove_const<long>::type, long>::value), RemoveConst);
COMPILE_ASSERT((is_same<remove_const<std::string>::type, std::string>::value),
               RemoveConst);

// remove_reference
COMPILE_ASSERT((is_same<remove_reference<int&>::type, int>::value),
               RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<long&>::type, long>::value),
               RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<std::string&>::type,
                std::string>::value), RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<const int&>::type, const int>::value),
               RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<const long&>::type, const long>::value),
               RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<const std::string&>::type,
                const std::string>::value), RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<int>::type, int>::value), RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<long>::type, long>::value), RemoveReference);
COMPILE_ASSERT((is_same<remove_reference<std::string>::type, std::string>::value),
               RemoveReference);



}  // namespace
}  // namespace butil
