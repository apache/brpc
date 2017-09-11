// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BVAR_DETAIL_IS_ATOMICAL_H
#define BVAR_DETAIL_IS_ATOMICAL_H

#include "butil/type_traits.h"

namespace bvar {
namespace detail {
template <class T> struct is_atomical
: butil::integral_constant<bool, (butil::is_integral<T>::value ||
                                 butil::is_floating_point<T>::value)
                                 // FIXME(gejun): Not work in gcc3.4
                                 // butil::is_enum<T>::value ||
                                 // NOTE(gejun): Ops on pointers are not
                                 // atomic generally
                                 // butil::is_pointer<T>::value
                          > {};
template <class T> struct is_atomical<const T> : is_atomical<T> { };
template <class T> struct is_atomical<volatile T> : is_atomical<T> { };
template <class T> struct is_atomical<const volatile T> : is_atomical<T> { };

}  // namespace detail
}  // namespace bvar

#endif
