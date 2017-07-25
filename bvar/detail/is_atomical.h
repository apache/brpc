#ifndef BVAR_DETAIL_IS_ATOMICAL_H
#define BVAR_DETAIL_IS_ATOMICAL_H

#include "base/type_traits.h"

namespace bvar {
namespace detail {
template <class T> struct is_atomical
: base::integral_constant<bool, (base::is_integral<T>::value ||
                                 base::is_floating_point<T>::value)
                                 // FIXME(gejun): Not work in gcc3.4
                                 // base::is_enum<T>::value ||
                                 // NOTE(gejun): Ops on pointers are not
                                 // atomic generally
                                 // base::is_pointer<T>::value
                          > {};
template <class T> struct is_atomical<const T> : is_atomical<T> { };
template <class T> struct is_atomical<volatile T> : is_atomical<T> { };
template <class T> struct is_atomical<const volatile T> : is_atomical<T> { };

}  // namespace detail
}  // namespace bvar

#endif
