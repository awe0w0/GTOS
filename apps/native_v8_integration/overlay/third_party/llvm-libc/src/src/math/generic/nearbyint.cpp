//===-- Implementation of nearbyint function ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/nearbyint.h"
#include "src/__support/math/nearbyint.h"

namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(double, nearbyint, (double x)) {
#if defined(__GTOS__) && defined(__i386__)
  // Mode detection must not perform arithmetic that can raise FE_INEXACT.
  unsigned mxcsr;
  asm volatile("stmxcsr %0" : "=m"(mxcsr));
  constexpr int directions[] = {FP_INT_TONEAREST, FP_INT_DOWNWARD, FP_INT_UPWARD,
                                FP_INT_TOWARDZERO};
  return fputil::round_using_specific_rounding_mode(
      x, directions[(mxcsr >> 13) & 3]);
#else
  return math::nearbyint(x);
#endif
}

} // namespace LIBC_NAMESPACE_DECL
