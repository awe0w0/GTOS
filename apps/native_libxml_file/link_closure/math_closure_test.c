/*
 * Unit test for math_closure.cpp, on the host, against the host's libm.
 *
 * This tests the real source file: the closure is compiled with -DGTOS_MATH_TEST, which prefixes its definitions (gtos_fabs, …) so they do
 * not collide with the host's, and every value is compared with the host's corresponding function over a grid that covers what xpath.c does
 * (powers of ten for number formatting, rounding at the half-integer boundary, remainder of same-magnitude operands, logarithms of numbers
 * spanning many decades).
 *
 * The test says what it covers and what it does not: it samples ranges, so a passing run is evidence for those ranges, not a proof over all
 * doubles.
 *
 * The comparison itself had to be fixed before its numbers meant anything. It used to return early when `want` was zero or NaN, which meant
 * every zero result — pow(0, 3), log10(1), fabs(+0), and both negative zeros — was never checked, and a NaN `got` against a finite `want`
 * produced a NaN error that compares false against the tolerance and therefore passed. Now: a zero wants a zero of the same sign, a NaN wants
 * a NaN, an infinity wants the same infinity, and only finite non-zero pairs use a relative error.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
double gtos_fabs(double);
double gtos_floor(double);
double gtos_ceil(double);
double gtos_fmod(double, double);
double gtos_log10(double);
double gtos_pow(double, double);
#ifdef __cplusplus
}
#endif

static double max_relative_error;
static int comparisons;
static int failures;

static int sign_bit(double value) {
  union {
    double d;
    unsigned long long u;
  } bits = {value};
  return (int)(bits.u >> 63);
}

static int same_sign(double a, double b) { return sign_bit(a) == sign_bit(b); }

static void fail(const char *name, double got, double want, double input_a, double input_b, const char *why) {
  ++failures;
  printf("FAIL %s(%g,%g): got %.17g want %.17g — %s\n", name, input_a, input_b, got, want, why);
}

static void record(const char *name, double got, double want, double input_a, double input_b) {
  ++comparisons;
  if (want != want) {
    if (got == got) fail(name, got, want, input_a, input_b, "a NaN result was expected");
    return;
  }
  if (got != got) {
    fail(name, got, want, input_a, input_b, "produced NaN for a non-NaN result");
    return;
  }
  if (want == 0.0) {
    if (got != 0.0) {
      fail(name, got, want, input_a, input_b, "a zero result was expected");
      return;
    }
    if (!same_sign(got, want)) fail(name, got, want, input_a, input_b, "the zero has the wrong sign");
    return;
  }
  if (want > 1e308 * 1.7976931348623157 || want < -1e308 * 1.7976931348623157) {
    if (got != want) fail(name, got, want, input_a, input_b, "an infinite result was expected");
    return;
  }
  if (got > 1.7976931348623157e308 || got < -1.7976931348623157e308) {
    fail(name, got, want, input_a, input_b, "produced an infinity for a finite result");
    return;
  }
  const double error = fabs((got - want) / want);
  if (error > max_relative_error) max_relative_error = error;
  if (error > 1e-9) fail(name, got, want, input_a, input_b, "relative error above 1e-9");
}

int main(void) {
  int checked = 0;

  for (double value = -1000.5; value <= 1000.5; value += 0.5) {
    record("fabs", gtos_fabs(value), fabs(value), value, 0.0);
    record("floor", gtos_floor(value), floor(value), value, 0.0);
    record("ceil", gtos_ceil(value), ceil(value), value, 0.0);
    checked += 3;
  }
  // The boundary xpath.c rounds at (in - floor(in) >= 0.5) and the values just either side of it.
  const double boundaries[] = {0.4999999999999999, 0.5, 0.5000000000000001, -0.5, -0.5000000000000001, 1e15, -1e15};
  for (unsigned index = 0; index < sizeof(boundaries) / sizeof(boundaries[0]); ++index) {
    record("floor", gtos_floor(boundaries[index]), floor(boundaries[index]), boundaries[index], 0.0);
    record("ceil", gtos_ceil(boundaries[index]), ceil(boundaries[index]), boundaries[index], 0.0);
    checked += 2;
  }

  const double divisors[] = {3.0, 7.0, 10.0, 0.5, 1.0 / 3.0, 1024.0};
  for (unsigned index = 0; index < sizeof(divisors) / sizeof(divisors[0]); ++index) {
    for (double value = -1000.0; value <= 1000.0; value += 0.25) {
      record("fmod", gtos_fmod(value, divisors[index]), fmod(value, divisors[index]), value, divisors[index]);
      checked += 1;
    }
  }

  for (double exponent = -300.0; exponent <= 300.0; exponent += 0.5) {
    record("log10", gtos_log10(pow(10.0, exponent)), log10(pow(10.0, exponent)), pow(10.0, exponent), 0.0);
    checked += 1;
  }
  const double magnitudes[] = {1e-300, 1e-30, 0.1, 1.0, 2.0, 10.0, 1e5, 1e30, 1e300};
  for (unsigned index = 0; index < sizeof(magnitudes) / sizeof(magnitudes[0]); ++index) {
    record("log10", gtos_log10(magnitudes[index]), log10(magnitudes[index]), magnitudes[index], 0.0);
    checked += 1;
  }

  // pow is used as pow(10.0, exponent) for number formatting; sweep that and a set of general bases.
  const double bases[] = {10.0, 2.0, 0.5, 1.5, 1e-3, 1e5, -2.0, -0.5};
  for (unsigned index = 0; index < sizeof(bases) / sizeof(bases[0]); ++index) {
    for (double exponent = -30.0; exponent <= 30.0; exponent += 0.5) {
      record("pow", gtos_pow(bases[index], exponent), pow(bases[index], exponent), bases[index], exponent);
      checked += 1;
    }
  }
  record("pow", gtos_pow(10.0, 0.0), pow(10.0, 0.0), 10.0, 0.0);
  record("pow", gtos_pow(0.0, 3.0), pow(0.0, 3.0), 0.0, 3.0);
  record("pow", gtos_pow(-0.0, 0.5), pow(-0.0, 0.5), -0.0, 0.5);
  record("pow", gtos_pow(-0.0, -1.0), pow(-0.0, -1.0), -0.0, -1.0);
  record("pow", gtos_pow(-0.0, -2.0), pow(-0.0, -2.0), -0.0, -2.0);
  record("pow", gtos_pow(-0.0, 0.0), pow(-0.0, 0.0), -0.0, 0.0);
  record("pow", gtos_pow(0.0 / 0.0, 0.0), pow(0.0 / 0.0, 0.0), 0.0 / 0.0, 0.0);


  // The results the old comparison skipped outright: signed zeros, the identities, subnormal logarithms, and the places where the answer is
  // NaN or an infinity rather than a number.
  const double zeros[] = {0.0, -0.0};
  for (unsigned index = 0; index < sizeof(zeros) / sizeof(zeros[0]); ++index) {
    record("fabs", gtos_fabs(zeros[index]), fabs(zeros[index]), zeros[index], 0.0);
    record("floor", gtos_floor(zeros[index]), floor(zeros[index]), zeros[index], 0.0);
    record("ceil", gtos_ceil(zeros[index]), ceil(zeros[index]), zeros[index], 0.0);
    record("log10", gtos_log10(zeros[index] == 0.0 ? 1.0 : 1.0), log10(1.0), 1.0, 0.0);
  }
  // floor/ceil keep the sign of the zero they produce, which is the case that used to disappear.
  record("floor", gtos_floor(-0.0), floor(-0.0), -0.0, 0.0);
  record("ceil", gtos_ceil(-0.5), ceil(-0.5), -0.5, 0.0);
  record("ceil", gtos_ceil(-0.0), ceil(-0.0), -0.0, 0.0);
  // Subnormals and the smallest normal: log10 must not flush them to zero or lose the decade.
  const double tiny[] = {5e-324, 1e-310, 2.2250738585072014e-308, 1e-300};
  for (unsigned index = 0; index < sizeof(tiny) / sizeof(tiny[0]); ++index)
    record("log10", gtos_log10(tiny[index]), log10(tiny[index]), tiny[index], 0.0);
  // pow where the mathematically correct answer overflows, underflows, is NaN, or is an exact identity.
  record("pow", gtos_pow(10.0, 400.0), pow(10.0, 400.0), 10.0, 400.0);
  record("pow", gtos_pow(-2.0, 0.5), pow(-2.0, 0.5), -2.0, 0.5);
  record("pow", gtos_pow(0.0, 0.0), pow(0.0, 0.0), 0.0, 0.0);
  record("pow", gtos_pow(10.0, -400.0), pow(10.0, -400.0), 10.0, -400.0);
  record("fmod", gtos_fmod(1e300, 1e300), fmod(1e300, 1e300), 1e300, 1e300);
  record("fmod", gtos_fmod(-6.0, 4.0), fmod(-6.0, 4.0), -6.0, 4.0);

  // The infinite and not-a-number boundaries, including the divisions that produce them: an infinity input, an infinity divisor, a zero
  // divisor, and the exact largest finite power of two (which the old 709.0 threshold in exp() used to reject).
  const double infinity = 1.0 / 0.0;
  record("pow", gtos_pow(2.0, 1023.0), pow(2.0, 1023.0), 2.0, 1023.0);
  record("pow", gtos_pow(2.0, 1024.0), pow(2.0, 1024.0), 2.0, 1024.0);
  record("pow", gtos_pow(2.0, -1074.0), pow(2.0, -1074.0), 2.0, -1074.0);
  record("pow", gtos_pow(2.0, 1023.75), pow(2.0, 1023.75), 2.0, 1023.75);
  record("pow", gtos_pow(2.0, 1023.9), pow(2.0, 1023.9), 2.0, 1023.9);
  record("pow", gtos_pow(2.0, 709.78), pow(2.0, 709.78), 2.0, 709.78);
  record("pow", gtos_pow(2.0, -1075.0), pow(2.0, -1075.0), 2.0, -1075.0);
  record("pow", gtos_pow(-0.0, 3.0), pow(-0.0, 3.0), -0.0, 3.0);
  record("pow", gtos_pow(-0.0, 2.0), pow(-0.0, 2.0), -0.0, 2.0);
  record("pow", gtos_pow(-infinity, 3.0), pow(-infinity, 3.0), -infinity, 3.0);
  record("pow", gtos_pow(-infinity, 2.0), pow(-infinity, 2.0), -infinity, 2.0);
  {
    // 1^anything is 1 and anything^0 is 1, including the not-a-number argument: these are the pow cases whose result must ignore a NaN input,
    // so they are checked directly instead of being inferred from a NaN result elsewhere.
    volatile double zero = 0.0;
    const double nan_value = zero / zero;
    record("pow", gtos_pow(1.0, nan_value), pow(1.0, nan_value), 1.0, nan_value);
    record("pow", gtos_pow(nan_value, 0.0), pow(nan_value, 0.0), nan_value, 0.0);
  }
  record("log10", gtos_log10(infinity), log10(infinity), infinity, 0.0);
  record("log10", gtos_log10(-0.0), log10(-0.0), -0.0, 0.0);
  record("log10", gtos_log10(-1.0), log10(-1.0), -1.0, 0.0);
  record("fmod", gtos_fmod(infinity, 3.0), fmod(infinity, 3.0), infinity, 3.0);
  record("fmod", gtos_fmod(3.0, infinity), fmod(3.0, infinity), 3.0, infinity);
  record("fmod", gtos_fmod(3.0, 0.0), fmod(3.0, 0.0), 3.0, 0.0);
  record("fabs", gtos_fabs(-infinity), fabs(-infinity), -infinity, 0.0);
  record("floor", gtos_floor(-infinity), floor(-infinity), -infinity, 0.0);
  record("ceil", gtos_ceil(infinity), ceil(infinity), infinity, 0.0);

  {
    volatile double zero = 0.0;
    const double quotient = 1.0 / zero;
    record("fabs", gtos_fabs(quotient), fabs(quotient), quotient, 0.0);
    const double undefined = zero / zero;
    record("fabs", gtos_fabs(undefined), fabs(undefined), undefined, 0.0);
  }

  if (failures != 0) {
    printf("math closure: %d failures in %d comparisons\n", failures, comparisons);
    return 1;
  }
  printf("math closure: %d comparisons, maximum relative error %.3e against the host libm\n", comparisons, max_relative_error);
  (void)checked;
  return 0;
}
