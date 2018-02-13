// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_MEMORY_RAW_SCOPED_REFPTR_MISMATCH_CHECKER_H_
#define BUTIL_MEMORY_RAW_SCOPED_REFPTR_MISMATCH_CHECKER_H_

#include "butil/memory/ref_counted.h"
#include "butil/type_traits.h"
#include "butil/build_config.h"

// It is dangerous to post a task with a T* argument where T is a subtype of
// RefCounted(Base|ThreadSafeBase), since by the time the parameter is used, the
// object may already have been deleted since it was not held with a
// scoped_refptr. Example: http://crbug.com/27191
// The following set of traits are designed to generate a compile error
// whenever this antipattern is attempted.

namespace butil {

// This is a base internal implementation file used by task.h and callback.h.
// Not for public consumption, so we wrap it in namespace internal.
namespace internal {

template <typename T>
struct NeedsScopedRefptrButGetsRawPtr {
#if defined(OS_WIN)
  enum {
    value = butil::false_type::value
  };
#else
  enum {
    // Human readable translation: you needed to be a scoped_refptr if you are a
    // raw pointer type and are convertible to a RefCounted(Base|ThreadSafeBase)
    // type.
    value = (is_pointer<T>::value &&
             (is_convertible<T, subtle::RefCountedBase*>::value ||
              is_convertible<T, subtle::RefCountedThreadSafeBase*>::value))
  };
#endif
};

template <typename Params>
struct ParamsUseScopedRefptrCorrectly {
  enum { value = 0 };
};

template <>
struct ParamsUseScopedRefptrCorrectly<Tuple0> {
  enum { value = 1 };
};

template <typename A>
struct ParamsUseScopedRefptrCorrectly<Tuple1<A> > {
  enum { value = !NeedsScopedRefptrButGetsRawPtr<A>::value };
};

template <typename A, typename B>
struct ParamsUseScopedRefptrCorrectly<Tuple2<A, B> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value) };
};

template <typename A, typename B, typename C>
struct ParamsUseScopedRefptrCorrectly<Tuple3<A, B, C> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value) };
};

template <typename A, typename B, typename C, typename D>
struct ParamsUseScopedRefptrCorrectly<Tuple4<A, B, C, D> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value) };
};

template <typename A, typename B, typename C, typename D, typename E>
struct ParamsUseScopedRefptrCorrectly<Tuple5<A, B, C, D, E> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F>
struct ParamsUseScopedRefptrCorrectly<Tuple6<A, B, C, D, E, F> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F, typename G>
struct ParamsUseScopedRefptrCorrectly<Tuple7<A, B, C, D, E, F, G> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value ||
                   NeedsScopedRefptrButGetsRawPtr<G>::value) };
};

template <typename A, typename B, typename C, typename D, typename E,
          typename F, typename G, typename H>
struct ParamsUseScopedRefptrCorrectly<Tuple8<A, B, C, D, E, F, G, H> > {
  enum { value = !(NeedsScopedRefptrButGetsRawPtr<A>::value ||
                   NeedsScopedRefptrButGetsRawPtr<B>::value ||
                   NeedsScopedRefptrButGetsRawPtr<C>::value ||
                   NeedsScopedRefptrButGetsRawPtr<D>::value ||
                   NeedsScopedRefptrButGetsRawPtr<E>::value ||
                   NeedsScopedRefptrButGetsRawPtr<F>::value ||
                   NeedsScopedRefptrButGetsRawPtr<G>::value ||
                   NeedsScopedRefptrButGetsRawPtr<H>::value) };
};

}  // namespace internal

}  // namespace butil

#endif  // BUTIL_MEMORY_RAW_SCOPED_REFPTR_MISMATCH_CHECKER_H_
