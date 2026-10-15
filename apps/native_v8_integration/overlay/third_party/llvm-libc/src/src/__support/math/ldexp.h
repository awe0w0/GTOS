//===-- Implementation header for ldexp -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_LDEXP_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_LDEXP_H

#include "src/__support/FPUtil/ManipulationFunctions.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {

LIBC_INLINE constexpr double ldexp(double x, int exp) {
  const fputil::FPBits<double> bits(x);
  // GTOS reports range errors uniformly in every rounding mode, including
  // finite saturation. Keep the pinned helper's exact result selection.
  if (exp != 0 && !bits.is_zero() && !bits.is_inf_or_nan()) {
    const fputil::DyadicFloat<fputil::FPBits<double>::STORAGE_LEN> normal(x);
    const long long scaled_exp =
        static_cast<long long>(normal.get_unbiased_exponent()) + exp;
    constexpr int MIN_SUBNORMAL_EXP = 1 - fputil::FPBits<double>::EXP_BIAS -
                                      fputil::FPBits<double>::FRACTION_LEN;
    if (LIBC_UNLIKELY(scaled_exp > fputil::FPBits<double>::EXP_BIAS)) {
      fputil::set_errno_if_required(ERANGE);
      fputil::raise_except_if_required(FE_OVERFLOW | FE_INEXACT);
    } else if (LIBC_UNLIKELY(scaled_exp < MIN_SUBNORMAL_EXP)) {
      fputil::set_errno_if_required(ERANGE);
      fputil::raise_except_if_required(FE_UNDERFLOW | FE_INEXACT);
    }
  }
  return fputil::ldexp(x, exp);
}

} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_LDEXP_H
