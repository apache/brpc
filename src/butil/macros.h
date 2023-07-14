// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains macros and macro-like constructs (e.g., templates) that
// are commonly used throughout Chromium source. (It may also contain things
// that are closely related to things that are commonly used that belong in this
// file.)

#ifndef BUTIL_MACROS_H_
#define BUTIL_MACROS_H_

#include <stddef.h>  // For size_t.
#include <string.h>  // For memcpy.
#include <stdlib.h>

#include "butil/compiler_specific.h"  // For ALLOW_UNUSED.
#include "butil/string_printf.h"      // For butil::string_printf().

// There must be many copy-paste versions of these macros which are same
// things, undefine them to avoid conflict.
#undef DISALLOW_COPY
#undef DISALLOW_ASSIGN
#undef DISALLOW_COPY_AND_ASSIGN
#undef DISALLOW_EVIL_CONSTRUCTORS
#undef DISALLOW_IMPLICIT_CONSTRUCTORS

#if !defined(BUTIL_CXX11_ENABLED)
#define BUTIL_DELETE_FUNCTION(decl) decl
#else
#define BUTIL_DELETE_FUNCTION(decl) decl = delete
#endif

// Put this in the private: declarations for a class to be uncopyable.
#define DISALLOW_COPY(TypeName)                         \
    BUTIL_DELETE_FUNCTION(TypeName(const TypeName&))

// Put this in the private: declarations for a class to be unassignable.
#define DISALLOW_ASSIGN(TypeName)                               \
    BUTIL_DELETE_FUNCTION(void operator=(const TypeName&))

// A macro to disallow the copy constructor and operator= functions
// This should be used in the private: declarations for a class
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                      \
    BUTIL_DELETE_FUNCTION(TypeName(const TypeName&));            \
    BUTIL_DELETE_FUNCTION(void operator=(const TypeName&))

// An older, deprecated, politically incorrect name for the above.
// NOTE: The usage of this macro was banned from our code base, but some
// third_party libraries are yet using it.
// TODO(tfarina): Figure out how to fix the usage of this macro in the
// third_party libraries and get rid of it.
#define DISALLOW_EVIL_CONSTRUCTORS(TypeName) DISALLOW_COPY_AND_ASSIGN(TypeName)

// A macro to disallow all the implicit constructors, namely the
// default constructor, copy constructor and operator= functions.
//
// This should be used in the private: declarations for a class
// that wants to prevent anyone from instantiating it. This is
// especially useful for classes containing only static methods.
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName) \
    BUTIL_DELETE_FUNCTION(TypeName());            \
    DISALLOW_COPY_AND_ASSIGN(TypeName)

// Concatenate numbers in c/c++ macros.
#ifndef BAIDU_CONCAT
# define BAIDU_CONCAT(a, b) BAIDU_CONCAT_HELPER(a, b)
# define BAIDU_CONCAT_HELPER(a, b) a##b
#endif

#undef arraysize
// The arraysize(arr) macro returns the # of elements in an array arr.
// The expression is a compile-time constant, and therefore can be
// used in defining new arrays, for example.  If you use arraysize on
// a pointer by mistake, you will get a compile-time error.
//
// One caveat is that arraysize() doesn't accept any array of an
// anonymous type or a type defined inside a function.  In these rare
// cases, you have to use the unsafe ARRAYSIZE_UNSAFE() macro below.  This is
// due to a limitation in C++'s template system.  The limitation might
// eventually be removed, but it hasn't happened yet.

// This template function declaration is used in defining arraysize.
// Note that the function doesn't need an implementation, as we only
// use its type.    
namespace butil {
template <typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
}

// That gcc wants both of these prototypes seems mysterious. VC, for
// its part, can't decide which to use (another mystery). Matching of
// template overloads: the final frontier.
#ifndef _MSC_VER
namespace butil {
template <typename T, size_t N>
char (&ArraySizeHelper(const T (&array)[N]))[N];
}
#endif

#define arraysize(array) (sizeof(::butil::ArraySizeHelper(array)))

// gejun: Following macro was used in other modules.
#undef ARRAY_SIZE
#define ARRAY_SIZE(array) arraysize(array)

// ARRAYSIZE_UNSAFE performs essentially the same calculation as arraysize,
// but can be used on anonymous types or types defined inside
// functions.  It's less safe than arraysize as it accepts some
// (although not all) pointers.  Therefore, you should use arraysize
// whenever possible.
//
// The expression ARRAYSIZE_UNSAFE(a) is a compile-time constant of type
// size_t.
//
// ARRAYSIZE_UNSAFE catches a few type errors.  If you see a compiler error
//
//   "warning: division by zero in ..."
//
// when using ARRAYSIZE_UNSAFE, you are (wrongfully) giving it a pointer.
// You should only use ARRAYSIZE_UNSAFE on statically allocated arrays.
//
// The following comments are on the implementation details, and can
// be ignored by the users.
//
// ARRAYSIZE_UNSAFE(arr) works by inspecting sizeof(arr) (the # of bytes in
// the array) and sizeof(*(arr)) (the # of bytes in one array
// element).  If the former is divisible by the latter, perhaps arr is
// indeed an array, in which case the division result is the # of
// elements in the array.  Otherwise, arr cannot possibly be an array,
// and we generate a compiler error to prevent the code from
// compiling.
//
// Since the size of bool is implementation-defined, we need to cast
// !(sizeof(a) & sizeof(*(a))) to size_t in order to ensure the final
// result has type size_t.
//
// This macro is not perfect as it wrongfully accepts certain
// pointers, namely where the pointer size is divisible by the pointee
// size.  Since all our code has to go through a 32-bit compiler,
// where a pointer is 4 bytes, this means all pointers to a type whose
// size is 3 or greater than 4 will be (righteously) rejected.
#undef ARRAYSIZE_UNSAFE
#define ARRAYSIZE_UNSAFE(a) \
    ((sizeof(a) / sizeof(*(a))) / \
     static_cast<size_t>(!(sizeof(a) % sizeof(*(a)))))

// Use implicit_cast as a safe version of static_cast or const_cast
// for upcasting in the type hierarchy (i.e. casting a pointer to Foo
// to a pointer to SuperclassOfFoo or casting a pointer to Foo to
// a const pointer to Foo).
// When you use implicit_cast, the compiler checks that the cast is safe.
// Such explicit implicit_casts are necessary in surprisingly many
// situations where C++ demands an exact type match instead of an
// argument type convertible to a target type.
//
// The From type can be inferred, so the preferred syntax for using
// implicit_cast is the same as for static_cast etc.:
//
//   implicit_cast<ToType>(expr)
//
// implicit_cast would have been part of the C++ standard library,
// but the proposal was submitted too late.  It will probably make
// its way into the language in the future.
namespace butil {
template<typename To, typename From>
inline To implicit_cast(From const &f) {
  return f;
}
}

#if defined(BUTIL_CXX11_ENABLED)

// C++11 supports compile-time assertion directly
#define BAIDU_CASSERT(expr, msg) static_assert(expr, #msg)

#else

// Assert constant boolean expressions at compile-time
// Params:
//   expr     the constant expression to be checked
//   msg      an error infomation conforming name conventions of C/C++
//            variables(alphabets/numbers/underscores, no blanks). For
//            example "cannot_accept_a_number_bigger_than_128" is valid
//            while "this number is out-of-range" is illegal.
//
// when an asssertion like "BAIDU_CASSERT(false, you_should_not_be_here)"
// breaks, a compilation error is printed:
//   
//   foo.cpp:401: error: enumerator value for `you_should_not_be_here___19' not
//   integer constant
//
// You can call BAIDU_CASSERT at global scope, inside a class or a function
// 
//   BAIDU_CASSERT(false, you_should_not_be_here);
//   int main () { ... }
//
//   struct Foo {
//       BAIDU_CASSERT(1 == 0, Never_equals);
//   };
//
//   int bar(...)
//   {
//       BAIDU_CASSERT (value < 10, invalid_value);
//   }
//
namespace butil {
template <bool> struct CAssert { static const int x = 1; };
template <> struct CAssert<false> { static const char * x; };
}

#define BAIDU_CASSERT(expr, msg)                                \
    enum { BAIDU_CONCAT(BAIDU_CONCAT(LINE_, __LINE__), __##msg) \
           = ::butil::CAssert<!!(expr)>::x };

#endif  // BUTIL_CXX11_ENABLED

// The impl. of chrome does not work for offsetof(Object, private_filed)
#undef COMPILE_ASSERT
#define COMPILE_ASSERT(expr, msg)  BAIDU_CASSERT(expr, msg)

// bit_cast<Dest,Source> is a template function that implements the
// equivalent of "*reinterpret_cast<Dest*>(&source)".  We need this in
// very low-level functions like the protobuf library and fast math
// support.
//
//   float f = 3.14159265358979;
//   int i = bit_cast<int32_t>(f);
//   // i = 0x40490fdb
//
// The classical address-casting method is:
//
//   // WRONG
//   float f = 3.14159265358979;            // WRONG
//   int i = * reinterpret_cast<int*>(&f);  // WRONG
//
// The address-casting method actually produces undefined behavior
// according to ISO C++ specification section 3.10 -15 -.  Roughly, this
// section says: if an object in memory has one type, and a program
// accesses it with a different type, then the result is undefined
// behavior for most values of "different type".
//
// This is true for any cast syntax, either *(int*)&f or
// *reinterpret_cast<int*>(&f).  And it is particularly true for
// conversions between integral lvalues and floating-point lvalues.
//
// The purpose of 3.10 -15- is to allow optimizing compilers to assume
// that expressions with different types refer to different memory.  gcc
// 4.0.1 has an optimizer that takes advantage of this.  So a
// non-conforming program quietly produces wildly incorrect output.
//
// The problem is not the use of reinterpret_cast.  The problem is type
// punning: holding an object in memory of one type and reading its bits
// back using a different type.
//
// The C++ standard is more subtle and complex than this, but that
// is the basic idea.
//
// Anyways ...
//
// bit_cast<> calls memcpy() which is blessed by the standard,
// especially by the example in section 3.9 .  Also, of course,
// bit_cast<> wraps up the nasty logic in one place.
//
// Fortunately memcpy() is very fast.  In optimized mode, with a
// constant size, gcc 2.95.3, gcc 4.0.1, and msvc 7.1 produce inline
// code with the minimal amount of data movement.  On a 32-bit system,
// memcpy(d,s,4) compiles to one load and one store, and memcpy(d,s,8)
// compiles to two loads and two stores.
//
// I tested this code with gcc 2.95.3, gcc 4.0.1, icc 8.1, and msvc 7.1.
//
// WARNING: if Dest or Source is a non-POD type, the result of the memcpy
// is likely to surprise you.
namespace butil {
template <class Dest, class Source>
inline Dest bit_cast(const Source& source) {
  COMPILE_ASSERT(sizeof(Dest) == sizeof(Source), VerifySizesAreEqual);

  Dest dest;
  memcpy(&dest, &source, sizeof(dest));
  return dest;
}
}  // namespace butil

// Used to explicitly mark the return value of a function as unused. If you are
// really sure you don't want to do anything with the return value of a function
// that has been marked WARN_UNUSED_RESULT, wrap it with this. Example:
//
//   scoped_ptr<MyType> my_var = ...;
//   if (TakeOwnership(my_var.get()) == SUCCESS)
//     ignore_result(my_var.release());
//
namespace butil {
template<typename T>
inline void ignore_result(const T&) {
}
} // namespace butil

// The following enum should be used only as a constructor argument to indicate
// that the variable has static storage class, and that the constructor should
// do nothing to its state.  It indicates to the reader that it is legal to
// declare a static instance of the class, provided the constructor is given
// the butil::LINKER_INITIALIZED argument.  Normally, it is unsafe to declare a
// static variable that has a constructor or a destructor because invocation
// order is undefined.  However, IF the type can be initialized by filling with
// zeroes (which the loader does for static variables), AND the destructor also
// does nothing to the storage, AND there are no virtual methods, then a
// constructor declared as
//       explicit MyClass(butil::LinkerInitialized x) {}
// and invoked as
//       static MyClass my_variable_name(butil::LINKER_INITIALIZED);
namespace butil {
enum LinkerInitialized { LINKER_INITIALIZED };

// Use these to declare and define a static local variable (static T;) so that
// it is leaked so that its destructors are not called at exit. If you need
// thread-safe initialization, use butil/lazy_instance.h instead.
#undef CR_DEFINE_STATIC_LOCAL
#define CR_DEFINE_STATIC_LOCAL(type, name, arguments) \
  static type& name = *new type arguments

}  // namespace butil

// Convert symbol to string
#ifndef BAIDU_SYMBOLSTR
# define BAIDU_SYMBOLSTR(a) BAIDU_SYMBOLSTR_HELPER(a)
# define BAIDU_SYMBOLSTR_HELPER(a) #a
#endif

#ifndef BAIDU_TYPEOF
# if defined(BUTIL_CXX11_ENABLED)
#  define BAIDU_TYPEOF decltype
# else
#  ifdef _MSC_VER
#   include <boost/typeof/typeof.hpp>
#   define BAIDU_TYPEOF BOOST_TYPEOF
#  else
#   define BAIDU_TYPEOF typeof
#  endif
# endif // BUTIL_CXX11_ENABLED
#endif  // BAIDU_TYPEOF

// ptr:     the pointer to the member.
// type:    the type of the container struct this is embedded in.
// member:  the name of the member within the struct.
#ifndef container_of
# define container_of(ptr, type, member) ({                             \
            const BAIDU_TYPEOF( ((type *)0)->member ) *__mptr = (ptr);  \
            (type *)( (char *)__mptr - offsetof(type,member) );})
#endif

// DEFINE_SMALL_ARRAY(MyType, my_array, size, 64);
//   my_array is typed `MyType*' and as long as `size'. If `size' is not
//   greater than 64, the array is allocated on stack.
//
// NOTE: NEVER use ARRAY_SIZE(my_array) which is always 1.

#if defined(__cplusplus)
namespace butil {
namespace internal {
template <typename T> struct ArrayDeleter {
    ArrayDeleter() : arr(0) {}
    ~ArrayDeleter() { delete[] arr; }
    T* arr;
};
}}

// Many versions of clang does not support variable-length array with non-pod
// types, have to implement the macro differently.
#if !defined(__clang__)
# define DEFINE_SMALL_ARRAY(Tp, name, size, maxsize)                    \
    Tp* name = 0;                                                       \
    const unsigned name##_size = (size);                                \
    const unsigned name##_stack_array_size = (name##_size <= (maxsize) ? name##_size : 0); \
    Tp name##_stack_array[name##_stack_array_size];                     \
    ::butil::internal::ArrayDeleter<Tp> name##_array_deleter;            \
    if (name##_stack_array_size) {                                      \
        name = name##_stack_array;                                      \
    } else {                                                            \
        name = new (::std::nothrow) Tp[name##_size];                    \
        name##_array_deleter.arr = name;                                \
    }
#else
// This implementation works for GCC as well, however it needs extra 16 bytes
// for ArrayCtorDtor.
namespace butil {
namespace internal {
template <typename T> struct ArrayCtorDtor {
    ArrayCtorDtor(void* arr, unsigned size) : _arr((T*)arr), _size(size) {
        for (unsigned i = 0; i < size; ++i) { new (_arr + i) T; }
    }
    ~ArrayCtorDtor() {
        for (unsigned i = 0; i < _size; ++i) { _arr[i].~T(); }
    }
private:
    T* _arr;
    unsigned _size;
};
}}
# define DEFINE_SMALL_ARRAY(Tp, name, size, maxsize)                    \
    Tp* name = 0;                                                       \
    const unsigned name##_size = (size);                                \
    const unsigned name##_stack_array_size = (name##_size <= (maxsize) ? name##_size : 0); \
    char name##_stack_array[sizeof(Tp) * name##_stack_array_size];      \
    ::butil::internal::ArrayDeleter<char> name##_array_deleter;          \
    if (name##_stack_array_size) {                                      \
        name = (Tp*)name##_stack_array;                                 \
    } else {                                                            \
        name = (Tp*)new (::std::nothrow) char[sizeof(Tp) * name##_size];\
        name##_array_deleter.arr = (char*)name;                         \
    }                                                                   \
    const ::butil::internal::ArrayCtorDtor<Tp> name##_array_ctor_dtor(name, name##_size);
#endif // !defined(__clang__)
#endif // defined(__cplusplus)

// Put following code somewhere global to run it before main():
// 
//   BAIDU_GLOBAL_INIT()
//   {
//       ... your code ...
//   }
//
// Your can:
//   * Write any code and access global variables.
//   * Use ASSERT_*.
//   * Have multiple BAIDU_GLOBAL_INIT() in one scope.
// 
// Since the code run in global scope, quit with exit() or similar functions.

#if defined(__cplusplus)
# define BAIDU_GLOBAL_INIT                                      \
namespace {  /*anonymous namespace */                           \
    struct BAIDU_CONCAT(BaiduGlobalInit, __LINE__) {            \
        BAIDU_CONCAT(BaiduGlobalInit, __LINE__)() { init(); }   \
        void init();                                            \
    } BAIDU_CONCAT(baidu_global_init_dummy_, __LINE__);         \
}  /* anonymous namespace */                                    \
    void BAIDU_CONCAT(BaiduGlobalInit, __LINE__)::init              
#else
# define BAIDU_GLOBAL_INIT                      \
    static void __attribute__((constructor))    \
    BAIDU_CONCAT(baidu_global_init_, __LINE__)

#endif  // __cplusplus

#define ASSERT_LOG(fmt, ...)                                            \
    do {                                                                \
        std::string log = butil::string_printf(fmt, ## __VA_ARGS__);    \
        LOG(FATAL) << log;                                              \
    } while (false)

// Assert macro that can crash the process to generate a dump.
#define RELEASE_ASSERT(condition)   \
    do {                            \
        if (!(condition)) {         \
            ::abort();              \
        }                           \
    } while (false)

// Assert macro that can crash the process to generate a dump and
// supply a verbose explanation of what went wrong.
// For example:
//  std::vector<int> v;
//  ...
//  RELEASE_ASSERT_VERBOSE(v.empty(), "v should be empty, but with size=%zu", v.size());
#define RELEASE_ASSERT_VERBOSE(condition, fmt, ...)                                 \
    do {                                                                            \
        if (!(condition)) {                                                         \
            ASSERT_LOG("Assert failure: " #condition ". " #fmt, ## __VA_ARGS__);    \
            ::abort();                                                              \
        }                                                                           \
    } while (false)

#endif  // BUTIL_MACROS_H_
