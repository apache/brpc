// Copyright 2009 Howard Hinnant, Ion Gazta&ntilde;aga.
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
// See http://www.boost.org/libs/foreach for documentation

// This is a C++03 emulation of std::unique_ptr placed in namespace boost.
// Reference http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2800.pdf
//   for the latest unique_ptr specification, and
//   reference http://www.open-std.org/jtc1/sc22/wg21/docs/lwg-active.html
//   for any pending issues against this specification.

#ifndef BUTIL_UNIQUE_PTR_H
#define BUTIL_UNIQUE_PTR_H

#include "butil/build_config.h"

#if defined(BUTIL_CXX11_ENABLED)

#include <memory>                                        // std::unique_ptr

#elif !defined(BAIDU_NO_EMULATED_UNIQUE_PTR)

#include <algorithm>                // std::swap until C++11
#include "butil/type_traits.h"
#include "butil/macros.h"            // BAIDU_CASSERT

namespace std {

namespace up_detail {

typedef char one;
struct two {one _[2];};

// An is_convertible<From, To> that considers From an rvalue (consistent with C++0X).
//   This is a simplified version neglecting the types function, array, void and abstract types
//   I had to make a special case out of is_convertible<T,T> to make move-only
//   types happy.

namespace is_conv_imp {
template <class T> one test1(const T&);
template <class T> two test1(...);
template <class T> one test2(T);
template <class T> two test2(...);
template <class T> T source();
}

template <class T1, class T2>
struct is_convertible {
    static const bool value = sizeof(is_conv_imp::test1<T2>(is_conv_imp::source<T1>())) == 1;
};

template <class T>
struct is_convertible<T, T> {
    static const bool value = sizeof(is_conv_imp::test2<T>(is_conv_imp::source<T>())) == 1;
};

template <class T>
class rv {
    T& r_;

public:
    explicit rv(T& r) : r_(r) {}
    T* operator->() {return &r_;}
    T& operator*() {return r_;}
};

template <class T>
struct identity {
    typedef T type;
};

}  // up_detail

template <class T>
inline
typename butil::enable_if<
    !up_detail::is_convertible<T, up_detail::rv<T> >::value,
    T&
>::type
move(T& t) {
    return t;
}

template <class T>
inline
typename butil::enable_if <
    !up_detail::is_convertible<T, up_detail::rv<T> >::value,
    const T&
>::type
move(const T& t) {
    return t;
}

template <class T>
inline
typename butil::enable_if <
    up_detail::is_convertible<T, up_detail::rv<T> >::value,
    T
>::type
move(T& t) {
    return T(up_detail::rv<T>(t));
}

template <class T>
inline
typename butil::enable_if <
    butil::is_reference<T>::value,
    T
>::type
forward(typename up_detail::identity<T>::type t) {
    return t;
}

template <class T>
inline
typename butil::enable_if <
    !butil::is_reference<T>::value,
    T
>::type
forward(typename up_detail::identity<T>::type& t) {
    return move(t);
}

template <class T>
inline
typename butil::enable_if <
    !butil::is_reference<T>::value,
    T
>::type
forward(const typename up_detail::identity<T>::type& t) {
    return move(const_cast<T&>(t));
}

namespace up_detail {

// A move-aware but stripped-down compressed_pair which only optimizes storage for T2
template <class T1, class T2, bool = butil::is_empty<T2>::value>
class UniquePtrStorage {
    T1 t1_;
    T2 t2_;

    typedef typename butil::add_reference<T2>::type T2_reference;
    typedef typename butil::add_reference<const T2>::type T2_const_reference;

    UniquePtrStorage(const UniquePtrStorage&);
    UniquePtrStorage& operator=(const UniquePtrStorage&);
public:
    operator rv<UniquePtrStorage>() {return rv<UniquePtrStorage>(*this);}

    UniquePtrStorage() : t1_(), t2_() {}
    explicit UniquePtrStorage(T1 t1) : t1_(move(t1)), t2_() {}
    UniquePtrStorage(T1 t1, T2 t2) : t1_(move(t1)), t2_(forward<T2>(t2)) {}

          T1& first()       {return t1_;}
    const T1& first() const {return t1_;}

          T2_reference second()       {return t2_;}
    T2_const_reference second() const {return t2_;}
};

template <class T1, class T2>
class UniquePtrStorage<T1, T2, true> : private T2 {
    T1 t1_;
    typedef T2 t2_;

    UniquePtrStorage(const UniquePtrStorage&);
    UniquePtrStorage& operator=(const UniquePtrStorage&);
public:
    operator rv<UniquePtrStorage>() {return rv<UniquePtrStorage>(*this);}

    UniquePtrStorage() : t1_() {}
    explicit UniquePtrStorage(T1 t1) : t1_(move(t1)) {}
    UniquePtrStorage(T1 t1, T2 t2) : t2_(move(t2)), t1_(move(t1)) {}

          T1& first()       {return t1_;}
    const T1& first() const {return t1_;}

          T2& second()       {return *this;}
    const T2& second() const {return *this;}
};

template <class T1, class T2, bool b>
inline
void
swap(UniquePtrStorage<T1, T2, b>& x, UniquePtrStorage<T1, T2, b>& y) {
    using std::swap;
    swap(x.first(), y.first());
    swap(x.second(), y.second());
}

}  // up_detail

template <class T>
struct default_delete {
    default_delete() {}
    template <class U>
        default_delete(const default_delete<U>&,
            typename butil::enable_if<up_detail::is_convertible<U*, T*>::value>::type* = 0) {}

    void operator()(T* ptr) const {
        BAIDU_CASSERT(sizeof(T) > 0, T_is_empty);
        delete ptr;
    }
};

template <class T>
struct default_delete<T[]> {
    void operator()(T* ptr) const {
        BAIDU_CASSERT(sizeof(T) > 0, T_is_empty);
        delete [] ptr;
    }

private:

    template <class U> void operator()(U*) const;
};

namespace up_detail {

namespace pointer_type_imp {

template <class U> static two test(...);
template <class U> static one test(typename U::pointer* = 0);

}  // pointer_type_imp

template <class T>
struct has_pointer_type {
    static const bool value = sizeof(pointer_type_imp::test<T>(0)) == 1;
};

namespace pointer_type_imp {

template <class T, class D, bool = has_pointer_type<D>::value>
struct pointer_type {
    typedef typename D::pointer type;
};

template <class T, class D>
struct pointer_type<T, D, false> {
    typedef T* type;
};

}  // pointer_type_imp

template <class T, class D>
struct pointer_type {
    typedef typename pointer_type_imp::pointer_type<T,
        typename butil::remove_reference<D>::type>::type type;
};

}  // up_detail

template <class T, class D = default_delete<T> >
class unique_ptr {
public:
    typedef T element_type;
    typedef D deleter_type;
    typedef typename up_detail::pointer_type<element_type, deleter_type>::type pointer;

private:
    up_detail::UniquePtrStorage<pointer, deleter_type> ptr_;

    typedef typename butil::add_reference<deleter_type>::type deleter_reference;
    typedef typename butil::add_reference<const deleter_type>::type deleter_const_reference;

    struct nat {int for_bool_;};

    unique_ptr(unique_ptr&);
    unique_ptr& operator=(unique_ptr&);

public:
    operator up_detail::rv<unique_ptr>() {
        return up_detail::rv<unique_ptr>(*this);
    }
    unique_ptr(up_detail::rv<unique_ptr> r)
        : ptr_(r->release(), forward<deleter_type>(r->get_deleter())) {}
    unique_ptr& operator=(up_detail::rv<unique_ptr> r) {
        reset(r->release());
        ptr_.second() = move(r->get_deleter());
        return *this;
    }

    unique_ptr() {}

    explicit unique_ptr(pointer p) : ptr_(p) {}

    unique_ptr(pointer p, typename butil::conditional<butil::is_reference<D>::value,
               volatile typename butil::remove_reference<D>::type&, D>::type d)
        : ptr_(move(p), forward<D>(const_cast<typename butil::add_reference<D>::type>(d))) {}

    template <class U, class E>
    unique_ptr(unique_ptr<U, E> u,
               typename butil::enable_if <
               !butil::is_array<U>::value &&
               up_detail::is_convertible<typename unique_ptr<U>::pointer, pointer>::value
&&
               up_detail::is_convertible<E, deleter_type>::value &&
               (
                   !butil::is_reference<deleter_type>::value ||
                   butil::is_same<deleter_type, E>::value)
               >::type* = 0)
        : ptr_(u.release(), forward<D>(forward<E>(u.get_deleter()))) {}

    ~unique_ptr() {
        BAIDU_CASSERT(!butil::is_reference<deleter_type>::value, deleter_is_ref);
        BAIDU_CASSERT(!butil::is_pointer<deleter_type>::value, deleter_is_pointer);
        reset();
    }

    unique_ptr& operator=(int nat::*) {
        reset();
        return *this;
    }

    template <class U, class E>
    unique_ptr& operator=(unique_ptr<U, E> u) {
            reset(u.release());
            ptr_.second() = move(u.get_deleter());
            return *this;
        }

    typename butil::add_reference<T>::type operator*() const {return *get();}
    pointer operator->() const {return get();}
    pointer get() const {return ptr_.first();}
    deleter_reference       get_deleter()       {return ptr_.second();}
    deleter_const_reference get_deleter() const {return ptr_.second();}
    operator int nat::*() const {return get() ? &nat::for_bool_ : 0;}

    void reset(pointer p = pointer()) {
        pointer t = get();
        if (t != pointer())
            get_deleter()(t);
        ptr_.first() = p;
    }

    pointer release() {
        pointer tmp = get();
        ptr_.first() = pointer();
        return tmp;
    }

    void swap(unique_ptr& u) {up_detail::swap(ptr_, u.ptr_);}
};

template <class T, class D>
class unique_ptr<T[], D> {
public:
    typedef T element_type;
    typedef D deleter_type;
    typedef typename up_detail::pointer_type<element_type, deleter_type>::type pointer;

private:
    up_detail::UniquePtrStorage<pointer, deleter_type> ptr_;

    typedef typename butil::add_reference<deleter_type>::type deleter_reference;
    typedef typename butil::add_reference<const deleter_type>::type deleter_const_reference;

    struct nat {int for_bool_;};

    unique_ptr(unique_ptr&);
    unique_ptr& operator=(unique_ptr&);

public:
    operator up_detail::rv<unique_ptr>() {return up_detail::rv<unique_ptr>(*this);}
    unique_ptr(up_detail::rv<unique_ptr> r)
        : ptr_(r->release(), forward<deleter_type>(r->get_deleter())) {}
    unique_ptr& operator=(up_detail::rv<unique_ptr> r) {
        reset(r->release());
        ptr_.second() = move(r->get_deleter());
        return *this;
    }

    unique_ptr() {}

    explicit unique_ptr(pointer p) : ptr_(p) {}

    unique_ptr(pointer p, typename butil::conditional<butil::is_reference<D>::value,
               volatile typename butil::remove_reference<D>::type&, D>::type d)
        : ptr_(move(p), forward<D>(const_cast<typename butil::add_reference<D>::type>(d))) {}

    ~unique_ptr() {
        BAIDU_CASSERT(!butil::is_reference<deleter_type>::value, deleter_is_ref);
        BAIDU_CASSERT(!butil::is_pointer<deleter_type>::value, deleter_is_pointer);
        reset();
    }

    T& operator[](size_t i) const {return get()[i];}
    pointer get() const {return ptr_.first();}
    deleter_reference       get_deleter()       {return ptr_.second();}
    deleter_const_reference get_deleter() const {return ptr_.second();}
    operator int nat::*() const {return get() ? &nat::for_bool_ : 0;}

    void reset(pointer p = pointer()) {
        pointer t = get();
        if (t != pointer())
            get_deleter()(t);
        ptr_.first() = p;
    }

    pointer release() {
        pointer tmp = get();
        ptr_.first() = pointer();
        return tmp;
    }

    void swap(unique_ptr& u) {up_detail::swap(ptr_, u.ptr_);}
private:
    template <class U>
    explicit unique_ptr(
        U, typename butil::enable_if<up_detail::is_convertible<U, pointer>::value>::type* = 0);

    template <class U>
    unique_ptr(U, typename butil::conditional<butil::is_reference<D>::value,
               volatile typename butil::remove_reference<D>::type&, D>::type,
               typename butil::enable_if<up_detail::is_convertible<U, pointer>::value>::type* = 0);
};

template<class T, class D>
inline
void
swap(unique_ptr<T, D>& x, unique_ptr<T, D>& y) {
    x.swap(y);
}

template<class T1, class D1, class T2, class D2>
inline
bool
operator==(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return x.get() == y.get();
}

template<class T1, class D1, class T2, class D2>
inline
bool
operator!=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return !(x == y);
}

template<class T1, class D1, class T2, class D2> 
inline
bool
operator<(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return x.get() < y.get();
}

template<class T1, class D1, class T2, class D2> 
inline
bool
operator<=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return !(y < x);
}

template<class T1, class D1, class T2, class D2> 
inline
bool
operator>(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return y < x;
}

template<class T1, class D1, class T2, class D2> 
inline
bool
operator>=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y) {
    return !(x < y);
}

}  // namespace std

#endif  // BUTIL_CXX11_ENABLED
#endif  // BUTIL_UNIQUE_PTR_H
