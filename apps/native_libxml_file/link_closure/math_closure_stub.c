/*
 * The judge's own calibration control: deliberately wrong implementations of the same six symbols.
 *
 * A test that only ever passes proves nothing about its own ability to fail. This file is linked instead of math_closure.cpp for the
 * calibration run, and it is wrong in exactly the ways the fixed judge is supposed to notice: pow is off by a relative 1e-6, log10 returns the
 * natural logarithm, and ceil drops the sign of a zero. fabs, floor and fmod are correct so the failure is attributable rather than general.
 *
 * The calibration is expected to FAIL. A calibration run that passes means the judge has holes again, and that is the signal this file exists
 * to produce.
 */
#include <math.h>

#if defined(__cplusplus)
extern "C" {
#endif

double gtos_fabs(double x) { return fabs(x); }
double gtos_floor(double x) { return floor(x); }
double gtos_ceil(double x) { return x == 0.0 ? 0.0 : ceil(x); } /* perturbs -0.0 */

// Off by one part per million: small enough to be a real rounding difference, large enough to exceed the 1e-9 tolerance.
double gtos_pow(double x, double y) { return pow(x, y) * (1.0 + 1e-6); }

// The natural logarithm where the base-10 one was asked for: the classic "looks plausible" mistake.
double gtos_log10(double x) { return x > 0.0 ? log(x) : (x == 0.0 ? -1.0 / 0.0 : 0.0 / 0.0); }

double gtos_fmod(double x, double y) { return fmod(x, y); }

#if defined(__cplusplus)
}
#endif
