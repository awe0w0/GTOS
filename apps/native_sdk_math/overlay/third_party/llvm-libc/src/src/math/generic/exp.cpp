//===-- Double-precision e^x function -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/math/exp.h"
#include "src/__support/math/exp.h"
namespace LIBC_NAMESPACE_DECL {

LLVM_LIBC_FUNCTION(double, exp, (double x)) {
  const double result = math::exp(x);
#if defined(__GTOS__) && defined(__i386__)
  // Directed saturation returns a finite value before the generic overflow
  // path raises its exception. Preserve the result and the current FP mode.
  const uint64_t bits = fputil::FPBits<double>(x).uintval();
  if (bits >= 0x40862e42fefa39f0ULL && bits < 0x7ff0000000000000ULL)
    fputil::raise_except_if_required(FE_OVERFLOW);
#endif
  return result;
}

} // namespace LIBC_NAMESPACE_DECL
