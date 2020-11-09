#include <math.h>
#include "feature/split/dirichlet/gsl_inline.h"
#include "feature/split/dirichlet/gsl_rng.h"
#define GSL_SQRT_DBL_MIN   1.4916681462400413e-154
void ran_dirichlet (const gsl_rng * r, const size_t K, const double alpha[], double theta[]);



