// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUTIL_SAFE_CONVERSIONS_H_
#define BUTIL_SAFE_CONVERSIONS_H_

#include <limits>

#include "butil/logging.h"
#include "butil/numerics/safe_conversions_impl.h"

namespace butil {

// Convenience function that returns true if the supplied value is in range
// for the destination type.
template <typename Dst, typename Src>
inline bool IsValueInRangeForNumericType(Src value) {
  return internal::DstRangeRelationToSrcRange<Dst>(value) ==
         internal::RANGE_VALID;
}

// checked_cast<> is analogous to static_cast<> for numeric types,
// except that it CHECKs that the specified numeric conversion will not
// overflow or underflow. NaN source will always trigger a CHECK.
template <typename Dst, typename Src>
inline Dst checked_cast(Src value) {
  CHECK(IsValueInRangeForNumericType<Dst>(value));
  return static_cast<Dst>(value);
}

// saturated_cast<> is analogous to static_cast<> for numeric types, except
// that the specified numeric conversion will saturate rather than overflow or
// underflow. NaN assignment to an integral will trigger a CHECK condition.
template <typename Dst, typename Src>
inline Dst saturated_cast(Src value) {
  // Optimization for floating point values, which already saturate.
  if (std::numeric_limits<Dst>::is_iec559)
    return static_cast<Dst>(value);

  switch (internal::DstRangeRelationToSrcRange<Dst>(value)) {
    case internal::RANGE_VALID:
      return static_cast<Dst>(value);

    case internal::RANGE_UNDERFLOW:
      return std::numeric_limits<Dst>::min();

    case internal::RANGE_OVERFLOW:
      return std::numeric_limits<Dst>::max();

    // Should fail only on attempting to assign NaN to a saturated integer.
    case internal::RANGE_INVALID:
      CHECK(false);
      return std::numeric_limits<Dst>::max();
  }

  NOTREACHED();
  return static_cast<Dst>(value);
}

}  // namespace butil

#endif  // BUTIL_SAFE_CONVERSIONS_H_
