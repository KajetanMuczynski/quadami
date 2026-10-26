/* LD_PRELOAD shim: the double libm functions the CLI imports, computed in x87 long double and
   rounded once to double. glibc does not dispatch its long double functions on CPU features, so
   the result does not depend on FMA/AVX2. Research only. */
#define _GNU_SOURCE
#include <math.h>
double sin(double x) { return (double)sinl(x); }
double cos(double x) { return (double)cosl(x); }
void sincos(double x, double* s, double* c) { *s = (double)sinl(x); *c = (double)cosl(x); }
double atan2(double y, double x) { return (double)atan2l(y, x); }
double acos(double x) { return (double)acosl(x); }
double exp(double x) { return (double)expl(x); }
double log(double x) { return (double)logl(x); }
double log10(double x) { return (double)log10l(x); }
double pow(double x, double y) { return (double)powl(x, y); }
