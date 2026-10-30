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

static void record(const char *name, double got, double want, double input_a, double input_b) {
  if (want == 0.0 || want != want) return;
  const double error = fabs((got - want) / want);
  if (error > max_relative_error) max_relative_error = error;
  if (error > 1e-9) {
    printf("FAIL %s(%g%s%g): got %.17g want %.17g relative %.3e\n", name, input_a, name[0] == 'p' || name[0] == 'f' ? "," : "", input_b,
           got, want, error);
    exit(1);
  }
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

  printf("math closure: %d comparisons, maximum relative error %.3e against the host libm\n", checked, max_relative_error);
  return 0;
}
