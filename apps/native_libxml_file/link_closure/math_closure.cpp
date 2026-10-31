/*
 * The libm subset xpath.c needs, and nothing more.
 *
 * Why this exists: Chromium's libxml2 contract keeps XPath enabled, `xmlInitParserInternal` (threads.c) calls `xmlInitXPathInternal`, and
 * xpath.c uses `fabs`, `floor`, `ceil`, `fmod`, `log10` and `pow` — plus `exit` once. The pinned GTOS user-space set has no libm, so a link
 * of the full 26-source parsing set stops on exactly one symbol (`xmlInitXPathInternal`) until these are provided. Rather than leave the link
 * open or fake the call, this implements the six functions for the ranges XPath actually reaches: number-to-string scaling with powers of
 * ten, rounding of doubles, remainder, and the logarithm behind the exponent of a formatted number.
 *
 * Accuracy claim, measured not assumed: `math_closure_test.c` compares every function against the host's libm over a grid and prints the
 * maximum relative error; the numbers it prints are the claim. The algorithms are standard: exact bit operations for fabs, truncate-and-adjust
 * for floor/ceil, binary scaling for fmod (exact), atanh series for the logarithm, range-reduced Taylor for exp, and pow = exp(y·ln x).
 *
 * Not claimed: this is not a complete libm (no trig, no exp2/ldexp, no long double, no denormal-specific handling beyond what the series
 * produce), and the error figures are for the sampled ranges, not a proven bound over all doubles.
 */
#ifdef GTOS_MATH_TEST
#define fabs gtos_fabs
#define floor gtos_floor
#define ceil gtos_ceil
#define fmod gtos_fmod
#define log10 gtos_log10
#define pow gtos_pow
#define exit gtos_exit
#endif

#ifndef GTOS_MATH_TEST
#include <process/abi.h>
#endif

namespace {

const double kLn2 = 0.693147180559945309417232121458176568;
const double kLn10 = 2.302585092994045684017991454684364208;
const double kTwoPi = 6.283185307179586476925286766559005768;

union DoubleBits {
  double value;
  unsigned long long bits;
};

// An odd integer exponent is what decides the sign of a zero or infinite base. `fmod(y, 2) != 0` is not that test: it is also true for every
// non-integer, so pow(-0, 0.5) came back -0 and pow(-0, -1) came back +inf.
int is_odd_integer(double value) {
  // No fabs/floor/fmod here: those names are gtos_ macros at this point in the file, and this helper sits above their definitions.
  const double magnitude = value < 0.0 ? -value : value;
  if (magnitude != magnitude) return 0;                       // NaN
  const double truncated = (double)(long long)magnitude;       // the exponents this can decide are far below 2^63
  if (truncated != magnitude) return 0;                        // not an integer
  return ((long long)truncated & 1LL) != 0;                    // odd integers only
}

int is_nan(double value) { return value != value; }
int is_infinite(double value) { return !is_nan(value) && (value > 1.7976931348623157e308 || value < -1.7976931348623157e308); }

// ln(m) for m in [1, 2), through ln(m) = 2·atanh((m-1)/(m+1)); the series converges fast on that interval.
double ln_near_one(double m) {
  const double z = (m - 1.0) / (m + 1.0);
  const double z2 = z * z;
  double term = z;
  double sum = 0.0;
  for (int k = 1; k < 60; k += 2) {
    sum += term / (double)k;
    term *= z2;
  }
  return 2.0 * sum;
}

// exp(t) = 2^k · exp(r), with |r| <= ln2/2 and a 14-term Taylor series for exp(r).
double exp_series(double t) {
  // ln(DBL_MAX) = 709.7827128933839. A tighter threshold rejects finite results — pow(2, 1023) is 8.98846567431158e307 and its exponent
  // t = 1023*ln2 = 709.0896 used to fall into the overflow branch. The constant matters in the last digit: 1024*ln2 = 709.78271289338404
  // equals the rounded 709.782712893384, so a threshold written with the short constant lets pow(2,1024) through as a finite number.
  if (t > 709.7827128933839) return 1.0 / 0.0;
  // Below this the result is below the smallest subnormal (exp(-745.133...) = 5e-324), so it underflows to zero.
  if (t < -745.1332191019412) return 0.0;
  const double k = (t < 0.0 ? -0.5 : 0.5) + t / kLn2;
  long long shift = (long long)k;
  // The scaling loop squares 2.0 once per bit, so a shift of 1024 makes 2^1024 an intermediate and the product overflows even when the
  // exponent is finite: t = 1023.75*ln2 = 709.6 has only bit 10 set, and the sum < 1 that should bring it back arrives after the inf.
  // Clamping to 1023 puts the remainder (0.35 .. 0.69) in r, where the series stays below 2 and 2^1023 * sum is representable.
  if (shift > 1023) shift = 1023;
  const double r = t - (double)shift * kLn2;
  double term = 1.0;
  double sum = 1.0;
  for (int i = 1; i <= 18; ++i) {
    term *= r / (double)i;
    sum += term;
  }
  // Scale by 2^shift with a loop: no ldexp in the pinned set, and |shift| stays small for XPath's numbers.
  double scale = 1.0;
  double base = shift < 0 ? 0.5 : 2.0;
  unsigned long long steps = shift < 0 ? (unsigned long long)(-shift) : (unsigned long long)shift;
  while (steps != 0) {
    if (steps & 1ULL) scale *= base;
    base *= base;
    steps >>= 1;
  }
  return sum * scale;
}

double ln(double x) {
  // A subnormal has a zero exponent field, so the field is not the scale: multiply by 2^54 to reach the normal range and pay the 54·ln2 back
  // in the result. Without this log10 of anything below 2.2e-308 was off by tens of decades and the old test never looked at those values.
  double normalised = x;
  int extra = 0;
  DoubleBits bits = {x};
  if (((bits.bits >> 52) & 0x7ffULL) == 0 && (bits.bits & 0x000fffffffffffffULL) != 0) {
    normalised = x * 18014398509481984.0; // 2^54
    extra = -54;
    bits.value = normalised;
  }
  int exponent = (int)((bits.bits >> 52) & 0x7ffULL) - 1023;
  DoubleBits mantissa = {normalised};
  mantissa.bits = (bits.bits & 0x000fffffffffffffULL) | 0x3ff0000000000000ULL;
  double m = mantissa.value;
  if (m > 1.4142135623730951) {
    m *= 0.5;
    exponent += 1;
  }
  return (double)(exponent + extra) * kLn2 + ln_near_one(m);
}

} // namespace

extern "C" double fabs(double x) {
  DoubleBits bits = {x};
  bits.bits &= 0x7fffffffffffffffULL;
  return bits.value;
}

extern "C" double floor(double x) {
  // Zero is returned as given: the truncation below would turn -0.0 into +0.0, and floor(-0.0) is -0.0.
  if (x == 0.0) return x;
  if (is_nan(x) || is_infinite(x) || fabs(x) >= 4503599627370496.0) return x;
  const long long truncated = (long long)x;
  const double result = (double)truncated;
  return result > x ? result - 1.0 : result;
}

extern "C" double ceil(double x) {
  if (x == 0.0) return x;
  if (is_nan(x) || is_infinite(x) || fabs(x) >= 4503599627370496.0) return x;
  const long long truncated = (long long)x;
  // ceil of a value in (-1, 0) is -0.0, not +0.0: the truncation to zero loses the sign that the caller can observe.
  if (truncated == 0 && x < 0.0) return -0.0;
  const double result = (double)truncated;
  return result < x ? result + 1.0 : result;
}

extern "C" double fmod(double x, double y) {
  if (is_nan(x) || is_nan(y) || is_infinite(x) || y == 0.0) return 1.0 / 0.0 - 1.0 / 0.0;
  if (is_infinite(y)) return x;
  double remainder = fabs(x);
  const double divisor = fabs(y);
  if (remainder < divisor) return x;
  // Binary scaling keeps every subtraction exact: remainder and divisor are the same floating value scaled by powers of two.
  double scaled = divisor;
  while (scaled * 2.0 <= remainder) scaled *= 2.0;
  while (remainder >= divisor) {
    while (scaled > remainder) scaled *= 0.5;
    remainder -= scaled;
  }
  return x < 0.0 ? -remainder : remainder;
}

extern "C" double log10(double x) {
  if (is_nan(x)) return x;
  if (x == 0.0) return -1.0 / 0.0;
  if (x < 0.0) return 1.0 / 0.0 - 1.0 / 0.0;
  if (is_infinite(x)) return x;
  return ln(x) / kLn10;
}

extern "C" double pow(double x, double y) {
  if (y == 0.0) return 1.0;
  // 1 to any power is 1, including a not-a-number exponent: IEEE 754 makes this one of the cases whose result ignores the NaN, and the test
  // checks it directly because the generic NaN branch below would otherwise answer NaN.
  // x^0 is 1 for every x, NaN and zero included, so this is decided before the NaN test.
  if (y == 0.0) return 1.0;
  if (x == 1.0) return 1.0;
  if (is_nan(x) || is_nan(y)) return 1.0 / 0.0 - 1.0 / 0.0;
  if (x == 0.0) {
    // `x == 0.0` cannot tell the two zeros apart, so the sign comes from the value itself, and only an odd integer exponent keeps it.
    const int signed_zero = is_odd_integer(y) && __builtin_signbit(x);
    if (y > 0.0) return signed_zero ? -0.0 : 0.0;
    // A negative exponent divides by a zero: the result is an infinity, negative only for -0 raised to an odd negative integer.
    return signed_zero ? -1.0 / 0.0 : 1.0 / 0.0;
  }
  const int negative_base = x < 0.0;
  if (negative_base) {
    const double rounded = floor(y);
    if (rounded != y) return 1.0 / 0.0 - 1.0 / 0.0;
  }
  const double magnitude = exp_series(y * ln(fabs(x)));
  return negative_base && is_odd_integer(y) ? -magnitude : magnitude;
}

// xpath.c's one call is a fatal path. GTOS has a real process exit (GTOS_SYS_EXIT, EBX = status), so this terminates the process the same way
// the loader expects instead of unwinding into nothing.
extern "C" void exit(int status) {
#ifndef GTOS_MATH_TEST
  unsigned operation = GTOS_SYS_EXIT;
  asm volatile("int $0x80" : "+a"(operation) : "b"((unsigned)status) : "memory", "cc");
#else
  (void)status;
#endif
  for (;;) {
  }
}
