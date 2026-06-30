/* Stub for VML-FT __xxx_finite symbols (glibc >= 2.31 removed them) */
#include <math.h>

#define MAKE_STUB_D(name) double __##name##_finite(double x) { return name(x); }
#define MAKE_STUB_F(name) float __##name##f_finite(float x) { return name##f(x); }
#define MAKE_STUB_D2(name) double __##name##_finite(double x, double y) { return name(x, y); }
#define MAKE_STUB_F2(name) float __##name##f_finite(float x, float y) { return name##f(x, y); }

MAKE_STUB_F(asin) MAKE_STUB_F(acos) MAKE_STUB_F(atanh) MAKE_STUB_F(acosh)
MAKE_STUB_F(sin)  MAKE_STUB_F(cos)  MAKE_STUB_F(tan)
MAKE_STUB_F(sinh)  MAKE_STUB_F(cosh) MAKE_STUB_F(tanh)
MAKE_STUB_F(exp)   MAKE_STUB_F(exp2) MAKE_STUB_F(exp10) MAKE_STUB_F(expm1)
MAKE_STUB_F(log)   MAKE_STUB_F(log2) MAKE_STUB_F(log10) MAKE_STUB_F(log1p)
MAKE_STUB_F(sqrt)  MAKE_STUB_F(cbrt)
MAKE_STUB_F2(atan2) MAKE_STUB_F2(fmod) MAKE_STUB_F2(pow) MAKE_STUB_F2(remainder)
MAKE_STUB_F(lgamma) MAKE_STUB_F(gamma)

MAKE_STUB_D(asin) MAKE_STUB_D(acos) MAKE_STUB_D(atanh) MAKE_STUB_D(acosh)
MAKE_STUB_D(sin)  MAKE_STUB_D(cos)  MAKE_STUB_D(tan)
MAKE_STUB_D(sinh)  MAKE_STUB_D(cosh) MAKE_STUB_D(tanh)
MAKE_STUB_D(exp)   MAKE_STUB_D(exp2) MAKE_STUB_D(exp10) MAKE_STUB_D(expm1)
MAKE_STUB_D(log)   MAKE_STUB_D(log2) MAKE_STUB_D(log10) MAKE_STUB_D(log1p)
MAKE_STUB_D(sqrt)  MAKE_STUB_D(cbrt)
MAKE_STUB_D2(atan2) MAKE_STUB_D2(fmod) MAKE_STUB_D2(pow) MAKE_STUB_D2(remainder)
MAKE_STUB_D(lgamma) MAKE_STUB_D(gamma)

/* _r variants */
double __gamma_r_finite(double x, int *signgamp) { return gamma(x); }
float __gammaf_r_finite(float x, int *signgamp) { return gammaf(x); }
double __lgamma_r_finite(double x, int *signgamp) { return lgamma_r(x, signgamp); }
float __lgammaf_r_finite(float x, int *signgamp) { return lgammaf_r(x, signgamp); }
