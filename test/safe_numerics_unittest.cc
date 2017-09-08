// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(COMPILER_MSVC) && defined(ARCH_CPU_32_BITS)
#include <mmintrin.h>
#endif
#include <stdint.h>

#include <limits>

#include "butil/compiler_specific.h"
#include "butil/numerics/safe_conversions.h"
#include "butil/numerics/safe_math.h"
#include "butil/type_traits.h"
#include <gtest/gtest.h>

using std::numeric_limits;
using butil::CheckedNumeric;
using butil::checked_cast;
using butil::saturated_cast;
using butil::internal::MaxExponent;
using butil::internal::RANGE_VALID;
using butil::internal::RANGE_INVALID;
using butil::internal::RANGE_OVERFLOW;
using butil::internal::RANGE_UNDERFLOW;
using butil::enable_if;

// MSVS 2013 ia32 may not reset the FPU between calculations, and the test
// framework masks the exceptions. So we just force a manual reset after NaN.
inline void ResetFloatingPointUnit() {
#if defined(COMPILER_MSVC) && defined(ARCH_CPU_32_BITS)
  _mm_empty();
#endif
}

// Helper macros to wrap displaying the conversion types and line numbers.
#define TEST_EXPECTED_VALIDITY(expected, actual)                           \
  EXPECT_EQ(expected, CheckedNumeric<Dst>(actual).validity())              \
      << "Result test: Value " << +(actual).ValueUnsafe() << " as " << dst \
      << " on line " << line;

#define TEST_EXPECTED_VALUE(expected, actual)                                \
  EXPECT_EQ(static_cast<Dst>(expected),                                      \
            CheckedNumeric<Dst>(actual).ValueUnsafe())                       \
      << "Result test: Value " << +((actual).ValueUnsafe()) << " as " << dst \
      << " on line " << line;

// Signed integer arithmetic.
template <typename Dst>
static void TestSpecializedArithmetic(
    const char* dst,
    int line,
    typename enable_if<
        numeric_limits<Dst>::is_integer&& numeric_limits<Dst>::is_signed,
        int>::type = 0) {
  typedef numeric_limits<Dst> DstLimits;
  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         -CheckedNumeric<Dst>(DstLimits::min()));
  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()).Abs());
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(-1).Abs());

  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::max()) + -1);
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) + -1);
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) + -DstLimits::max());

  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) - 1);
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) - -1);
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW,
      CheckedNumeric<Dst>(DstLimits::max()) - -DstLimits::max());
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) - DstLimits::max());

  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) * 2);

  TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) / -1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(-1) / 2);

  // Modulus is legal only for integers.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() % 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  TEST_EXPECTED_VALUE(-1, CheckedNumeric<Dst>(-1) % 2);
  TEST_EXPECTED_VALIDITY(RANGE_INVALID, CheckedNumeric<Dst>(-1) % -2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) % 2);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(DstLimits::max()) % 2);
  // Test all the different modulus combinations.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst %= 1);
}

// Unsigned integer arithmetic.
template <typename Dst>
static void TestSpecializedArithmetic(
    const char* dst,
    int line,
    typename enable_if<
        numeric_limits<Dst>::is_integer && !numeric_limits<Dst>::is_signed,
        int>::type = 0) {
  typedef numeric_limits<Dst> DstLimits;
  TEST_EXPECTED_VALIDITY(RANGE_VALID, -CheckedNumeric<Dst>(DstLimits::min()));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()).Abs());
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) + -1);
  TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW,
                         CheckedNumeric<Dst>(DstLimits::min()) - 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) * 2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) / 2);

  // Modulus is legal only for integers.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() % 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) % 2);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(DstLimits::min()) % 2);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(DstLimits::max()) % 2);
  // Test all the different modulus combinations.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 % CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) % 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst %= 1);
}

// Floating point arithmetic.
template <typename Dst>
void TestSpecializedArithmetic(
    const char* dst,
    int line,
    typename enable_if<numeric_limits<Dst>::is_iec559, int>::type = 0) {
  typedef numeric_limits<Dst> DstLimits;
  TEST_EXPECTED_VALIDITY(RANGE_VALID, -CheckedNumeric<Dst>(DstLimits::min()));

  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()).Abs());
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(-1).Abs());

  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) + -1);
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::max()) + 1);
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) + -DstLimits::max());

  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW,
      CheckedNumeric<Dst>(DstLimits::max()) - -DstLimits::max());
  TEST_EXPECTED_VALIDITY(
      RANGE_UNDERFLOW,
      CheckedNumeric<Dst>(-DstLimits::max()) - DstLimits::max());

  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) * 2);

  TEST_EXPECTED_VALUE(-0.5, CheckedNumeric<Dst>(-1.0) / 2);
  EXPECT_EQ(static_cast<Dst>(1.0), CheckedNumeric<Dst>(1.0).ValueFloating());
}

// Generic arithmetic tests.
template <typename Dst>
static void TestArithmetic(const char* dst, int line) {
  typedef numeric_limits<Dst> DstLimits;

  EXPECT_EQ(true, CheckedNumeric<Dst>().IsValid());
  EXPECT_EQ(false,
            CheckedNumeric<Dst>(CheckedNumeric<Dst>(DstLimits::max()) *
                                DstLimits::max()).IsValid());
  EXPECT_EQ(static_cast<Dst>(0), CheckedNumeric<Dst>().ValueOrDie());
  EXPECT_EQ(static_cast<Dst>(0), CheckedNumeric<Dst>().ValueOrDefault(1));
  EXPECT_EQ(static_cast<Dst>(1),
            CheckedNumeric<Dst>(CheckedNumeric<Dst>(DstLimits::max()) *
                                DstLimits::max()).ValueOrDefault(1));

  // Test the operator combinations.
  TEST_EXPECTED_VALUE(2, CheckedNumeric<Dst>(1) + CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) - CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) * CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(2, 1 + CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(0, 1 - CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, 1 * CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, 1 / CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(2, CheckedNumeric<Dst>(1) + 1);
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>(1) - 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) * 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / 1);
  CheckedNumeric<Dst> checked_dst = 1;
  TEST_EXPECTED_VALUE(2, checked_dst += 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(0, checked_dst -= 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(1, checked_dst *= 1);
  checked_dst = 1;
  TEST_EXPECTED_VALUE(1, checked_dst /= 1);

  // Generic negation.
  TEST_EXPECTED_VALUE(0, -CheckedNumeric<Dst>());
  TEST_EXPECTED_VALUE(-1, -CheckedNumeric<Dst>(1));
  TEST_EXPECTED_VALUE(1, -CheckedNumeric<Dst>(-1));
  TEST_EXPECTED_VALUE(static_cast<Dst>(DstLimits::max() * -1),
                      -CheckedNumeric<Dst>(DstLimits::max()));

  // Generic absolute value.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>().Abs());
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1).Abs());
  TEST_EXPECTED_VALUE(DstLimits::max(),
                      CheckedNumeric<Dst>(DstLimits::max()).Abs());

  // Generic addition.
  TEST_EXPECTED_VALUE(1, (CheckedNumeric<Dst>() + 1));
  TEST_EXPECTED_VALUE(2, (CheckedNumeric<Dst>(1) + 1));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(-1) + 1));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::min()) + 1);
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW, CheckedNumeric<Dst>(DstLimits::max()) + DstLimits::max());

  // Generic subtraction.
  TEST_EXPECTED_VALUE(-1, (CheckedNumeric<Dst>() - 1));
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>(1) - 1));
  TEST_EXPECTED_VALUE(-2, (CheckedNumeric<Dst>(-1) - 1));
  TEST_EXPECTED_VALIDITY(RANGE_VALID,
                         CheckedNumeric<Dst>(DstLimits::max()) - 1);

  // Generic multiplication.
  TEST_EXPECTED_VALUE(0, (CheckedNumeric<Dst>() * 1));
  TEST_EXPECTED_VALUE(1, (CheckedNumeric<Dst>(1) * 1));
  TEST_EXPECTED_VALUE(-2, (CheckedNumeric<Dst>(-1) * 2));
  TEST_EXPECTED_VALIDITY(
      RANGE_OVERFLOW, CheckedNumeric<Dst>(DstLimits::max()) * DstLimits::max());

  // Generic division.
  TEST_EXPECTED_VALUE(0, CheckedNumeric<Dst>() / 1);
  TEST_EXPECTED_VALUE(1, CheckedNumeric<Dst>(1) / 1);
  TEST_EXPECTED_VALUE(DstLimits::min() / 2,
                      CheckedNumeric<Dst>(DstLimits::min()) / 2);
  TEST_EXPECTED_VALUE(DstLimits::max() / 2,
                      CheckedNumeric<Dst>(DstLimits::max()) / 2);

  TestSpecializedArithmetic<Dst>(dst, line);
}

// Helper macro to wrap displaying the conversion types and line numbers.
#define TEST_ARITHMETIC(Dst) TestArithmetic<Dst>(#Dst, __LINE__)

TEST(SafeNumerics, SignedIntegerMath) {
  TEST_ARITHMETIC(int8_t);
  TEST_ARITHMETIC(int);
  TEST_ARITHMETIC(intptr_t);
  TEST_ARITHMETIC(intmax_t);
}

TEST(SafeNumerics, UnsignedIntegerMath) {
  TEST_ARITHMETIC(uint8_t);
  TEST_ARITHMETIC(unsigned int);
  TEST_ARITHMETIC(uintptr_t);
  TEST_ARITHMETIC(uintmax_t);
}

TEST(SafeNumerics, FloatingPointMath) {
  TEST_ARITHMETIC(float);
  TEST_ARITHMETIC(double);
}

// Enumerates the five different conversions types we need to test.
enum NumericConversionType {
  SIGN_PRESERVING_VALUE_PRESERVING,
  SIGN_PRESERVING_NARROW,
  SIGN_TO_UNSIGN_WIDEN_OR_EQUAL,
  SIGN_TO_UNSIGN_NARROW,
  UNSIGN_TO_SIGN_NARROW_OR_EQUAL,
};

// Template covering the different conversion tests.
template <typename Dst, typename Src, NumericConversionType conversion>
struct TestNumericConversion {};

// EXPECT_EQ wrappers providing specific detail on test failures.
#define TEST_EXPECTED_RANGE(expected, actual)                                  \
  EXPECT_EQ(expected, butil::internal::DstRangeRelationToSrcRange<Dst>(actual)) \
      << "Conversion test: " << src << " value " << actual << " to " << dst    \
      << " on line " << line;

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_PRESERVING_VALUE_PRESERVING> {
  static void Test(const char *dst, const char *src, int line) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
                   // Integral to floating.
    COMPILE_ASSERT((DstLimits::is_iec559 && SrcLimits::is_integer) ||
                   // Not floating to integral and...
                   (!(DstLimits::is_integer && SrcLimits::is_iec559) &&
                    // Same sign, same numeric, source is narrower or same.
                    ((SrcLimits::is_signed == DstLimits::is_signed &&
                     sizeof(Dst) >= sizeof(Src)) ||
                    // Or signed destination and source is smaller
                     (DstLimits::is_signed && sizeof(Dst) > sizeof(Src)))),
                   comparison_must_be_sign_preserving_and_value_preserving);

    const CheckedNumeric<Dst> checked_dst = SrcLimits::max();
    ;
    TEST_EXPECTED_VALIDITY(RANGE_VALID, checked_dst);
    if (MaxExponent<Dst>::value > MaxExponent<Src>::value) {
      if (MaxExponent<Dst>::value >= MaxExponent<Src>::value * 2 - 1) {
        // At least twice larger type.
        TEST_EXPECTED_VALIDITY(RANGE_VALID, SrcLimits::max() * checked_dst);

      } else {  // Larger, but not at least twice as large.
        TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, SrcLimits::max() * checked_dst);
        TEST_EXPECTED_VALIDITY(RANGE_VALID, checked_dst + 1);
      }
    } else {  // Same width type.
      TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + 1);
    }

    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max() * static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
      ResetFloatingPointUnit();
    } else if (numeric_limits<Src>::is_signed) {
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    }
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_PRESERVING_NARROW> {
  static void Test(const char *dst, const char *src, int line) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    COMPILE_ASSERT(SrcLimits::is_signed == DstLimits::is_signed,
                   destination_and_source_sign_must_be_the_same);
    COMPILE_ASSERT(sizeof(Dst) < sizeof(Src) ||
                   (DstLimits::is_integer && SrcLimits::is_iec559),
                   destination_must_be_narrower_than_source);

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst - SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::max() * -1);
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
      ResetFloatingPointUnit();
    } else if (SrcLimits::is_signed) {
      TEST_EXPECTED_VALUE(-1, checked_dst - static_cast<Src>(1));
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
      TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(-1));
    } else {
      TEST_EXPECTED_VALIDITY(RANGE_INVALID, checked_dst - static_cast<Src>(1));
      TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    }
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL> {
  static void Test(const char *dst, const char *src, int line) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    COMPILE_ASSERT(sizeof(Dst) >= sizeof(Src),
                   destination_must_be_equal_or_wider_than_source);
    COMPILE_ASSERT(SrcLimits::is_signed, source_must_be_signed);
    COMPILE_ASSERT(!DstLimits::is_signed, destination_must_be_unsigned);

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(SrcLimits::max(), checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + static_cast<Src>(-1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + -SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, static_cast<Src>(-1));
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, SIGN_TO_UNSIGN_NARROW> {
  static void Test(const char *dst, const char *src, int line) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    COMPILE_ASSERT((DstLimits::is_integer && SrcLimits::is_iec559) ||
                   (sizeof(Dst) < sizeof(Src)),
      destination_must_be_narrower_than_source);
    COMPILE_ASSERT(SrcLimits::is_signed, source_must_be_signed);
    COMPILE_ASSERT(!DstLimits::is_signed, destination_must_be_unsigned);

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + static_cast<Src>(-1));
    TEST_EXPECTED_VALIDITY(RANGE_UNDERFLOW, checked_dst + -SrcLimits::max());

    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
    TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, static_cast<Src>(-1));
    if (SrcLimits::is_iec559) {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::max() * -1);
      TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::infinity());
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::infinity() * -1);
      TEST_EXPECTED_RANGE(RANGE_INVALID, SrcLimits::quiet_NaN());
      ResetFloatingPointUnit();
    } else {
      TEST_EXPECTED_RANGE(RANGE_UNDERFLOW, SrcLimits::min());
    }
  }
};

template <typename Dst, typename Src>
struct TestNumericConversion<Dst, Src, UNSIGN_TO_SIGN_NARROW_OR_EQUAL> {
  static void Test(const char *dst, const char *src, int line) {
    typedef numeric_limits<Src> SrcLimits;
    typedef numeric_limits<Dst> DstLimits;
    COMPILE_ASSERT(sizeof(Dst) <= sizeof(Src),
                   destination_must_be_narrower_or_equal_to_source);
    COMPILE_ASSERT(!SrcLimits::is_signed, source_must_be_unsigned);
    COMPILE_ASSERT(DstLimits::is_signed, destination_must_be_signed);

    const CheckedNumeric<Dst> checked_dst;
    TEST_EXPECTED_VALUE(1, checked_dst + static_cast<Src>(1));
    TEST_EXPECTED_VALIDITY(RANGE_OVERFLOW, checked_dst + SrcLimits::max());
    TEST_EXPECTED_VALUE(SrcLimits::min(), checked_dst + SrcLimits::min());

    TEST_EXPECTED_RANGE(RANGE_VALID, SrcLimits::min());
    TEST_EXPECTED_RANGE(RANGE_OVERFLOW, SrcLimits::max());
    TEST_EXPECTED_RANGE(RANGE_VALID, static_cast<Src>(1));
  }
};

// Helper macro to wrap displaying the conversion types and line numbers
#define TEST_NUMERIC_CONVERSION(d, s, t) \
  TestNumericConversion<d, s, t>::Test(#d, #s, __LINE__)

TEST(SafeNumerics, IntMinOperations) {
  TEST_NUMERIC_CONVERSION(int8_t, int8_t, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(uint8_t, uint8_t, SIGN_PRESERVING_VALUE_PRESERVING);

  TEST_NUMERIC_CONVERSION(int8_t, int, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(uint8_t, unsigned int, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(int8_t, float, SIGN_PRESERVING_NARROW);

  TEST_NUMERIC_CONVERSION(uint8_t, int8_t, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);

  TEST_NUMERIC_CONVERSION(uint8_t, int, SIGN_TO_UNSIGN_NARROW);
  TEST_NUMERIC_CONVERSION(uint8_t, intmax_t, SIGN_TO_UNSIGN_NARROW);
  TEST_NUMERIC_CONVERSION(uint8_t, float, SIGN_TO_UNSIGN_NARROW);

  TEST_NUMERIC_CONVERSION(int8_t, unsigned int, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
  TEST_NUMERIC_CONVERSION(int8_t, uintmax_t, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
}

TEST(SafeNumerics, IntOperations) {
  TEST_NUMERIC_CONVERSION(int, int, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(unsigned int, unsigned int,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(int, int8_t, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(unsigned int, uint8_t,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(int, uint8_t, SIGN_PRESERVING_VALUE_PRESERVING);

  TEST_NUMERIC_CONVERSION(int, intmax_t, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(unsigned int, uintmax_t, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(int, float, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(int, double, SIGN_PRESERVING_NARROW);

  TEST_NUMERIC_CONVERSION(unsigned int, int, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);
  TEST_NUMERIC_CONVERSION(unsigned int, int8_t, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);

  TEST_NUMERIC_CONVERSION(unsigned int, intmax_t, SIGN_TO_UNSIGN_NARROW);
  TEST_NUMERIC_CONVERSION(unsigned int, float, SIGN_TO_UNSIGN_NARROW);
  TEST_NUMERIC_CONVERSION(unsigned int, double, SIGN_TO_UNSIGN_NARROW);

  TEST_NUMERIC_CONVERSION(int, unsigned int, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
  TEST_NUMERIC_CONVERSION(int, uintmax_t, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
}

TEST(SafeNumerics, IntMaxOperations) {
  TEST_NUMERIC_CONVERSION(intmax_t, intmax_t, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(uintmax_t, uintmax_t,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(intmax_t, int, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(uintmax_t, unsigned int,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(intmax_t, unsigned int,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(intmax_t, uint8_t, SIGN_PRESERVING_VALUE_PRESERVING);

  TEST_NUMERIC_CONVERSION(intmax_t, float, SIGN_PRESERVING_NARROW);
  TEST_NUMERIC_CONVERSION(intmax_t, double, SIGN_PRESERVING_NARROW);

  TEST_NUMERIC_CONVERSION(uintmax_t, int, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);
  TEST_NUMERIC_CONVERSION(uintmax_t, int8_t, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);

  TEST_NUMERIC_CONVERSION(uintmax_t, float, SIGN_TO_UNSIGN_NARROW);
  TEST_NUMERIC_CONVERSION(uintmax_t, double, SIGN_TO_UNSIGN_NARROW);

  TEST_NUMERIC_CONVERSION(intmax_t, uintmax_t, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
}

TEST(SafeNumerics, FloatOperations) {
  TEST_NUMERIC_CONVERSION(float, intmax_t, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(float, uintmax_t,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(float, int, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(float, unsigned int,
                          SIGN_PRESERVING_VALUE_PRESERVING);

  TEST_NUMERIC_CONVERSION(float, double, SIGN_PRESERVING_NARROW);
}

TEST(SafeNumerics, DoubleOperations) {
  TEST_NUMERIC_CONVERSION(double, intmax_t, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(double, uintmax_t,
                          SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(double, int, SIGN_PRESERVING_VALUE_PRESERVING);
  TEST_NUMERIC_CONVERSION(double, unsigned int,
                          SIGN_PRESERVING_VALUE_PRESERVING);
}

TEST(SafeNumerics, SizeTOperations) {
  TEST_NUMERIC_CONVERSION(size_t, int, SIGN_TO_UNSIGN_WIDEN_OR_EQUAL);
  TEST_NUMERIC_CONVERSION(int, size_t, UNSIGN_TO_SIGN_NARROW_OR_EQUAL);
}

TEST(SafeNumerics, CastTests) {
// MSVC catches and warns that we're forcing saturation in these tests.
// Since that's intentional, we need to shut this warning off.
#if defined(COMPILER_MSVC)
#pragma warning(disable : 4756)
#endif

  int small_positive = 1;
  int small_negative = -1;
  double double_small = 1.0;
  double double_large = numeric_limits<double>::max();
  double double_infinity = numeric_limits<float>::infinity();

  // Just test that the cast compiles, since the other tests cover logic.
  EXPECT_EQ(0, checked_cast<int>(static_cast<size_t>(0)));

  // Test various saturation corner cases.
  EXPECT_EQ(saturated_cast<int>(small_negative),
            static_cast<int>(small_negative));
  EXPECT_EQ(saturated_cast<int>(small_positive),
            static_cast<int>(small_positive));
  EXPECT_EQ(saturated_cast<unsigned>(small_negative),
            static_cast<unsigned>(0));
  EXPECT_EQ(saturated_cast<int>(double_small),
            static_cast<int>(double_small));
  EXPECT_EQ(saturated_cast<int>(double_large), numeric_limits<int>::max());
  EXPECT_EQ(saturated_cast<float>(double_large), double_infinity);
  EXPECT_EQ(saturated_cast<float>(-double_large), -double_infinity);
}
