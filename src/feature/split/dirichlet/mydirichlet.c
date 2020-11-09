/* mydirichlet.c is a simple implementation of the dirichlet distribution which generates an m-dimensional random set variables
 * It is inspired on the implemetnation from the gsl library omitting non-desired funtions
 * It may contain other distributions implmentations such as gamma
 * WDLC - Oct 2019
 */

#include <stdio.h> // Delete after please
#include "feature/split/dirichlet/mydirichlet.h"
#include <math.h>
#include "feature/split/dirichlet/gsl_inline.h"
#include "feature/split/dirichlet/gsl_rng.h"
#include <sys/time.h>

static void ran_dirichlet_small (const gsl_rng * r, const size_t K, const double alpha[], double theta[]);
static double gsl_ran_gamma (const gsl_rng * r, const double a, const double b);

double
gsl_rng_uniform_pos (const gsl_rng * r)
{
  double x;
  do
    {
      x = (r->type->get_double) (r->state) ;
    }
  while (x <= 0) ;
  return x ;
}

static double
gsl_ran_gamma (const gsl_rng * r, const double a, const double b)
{
  /* assume a > 0 */

  if (a < 1)
    {
      double u = gsl_rng_uniform_pos (r);
      return gsl_ran_gamma (r, 1.0 + a, b) * pow (u, 1.0 / a);
    }

  {
    double x, v, u;
    double d = a - 1.0 / 3.0;
    double c = (1.0 / 3.0) / sqrt (d);

    while (1)
      {
        do
          {
            x = gsl_ran_gaussian_ziggurat (r, 1.0);
            v = 1.0 + c * x;
          }
        while (v <= 0);

        v = v * v * v;
        u = gsl_rng_uniform_pos (r);

        if (u < 1 - 0.0331 * x * x * x * x) 
          break;

        if (log (u) < 0.5 * x * x + d * (1 - v + log (v)))
          break;
      }
    
    return b * d * v;
  }
}



/* When the values of alpha[] are small, scale the variates to avoid
   underflow so that the result is not 0/0.  Note that the Dirichlet
   distribution is defined by a ratio of gamma functions so we can
   take out an arbitrary factor to keep the values in the range of
   double precision. */

static void 
ran_dirichlet_small (const gsl_rng * r, const size_t K,
                     const double alpha[], double theta[])
{
  size_t i;
  double norm = 0.0, umax = 0;

  for (i = 0; i < K; i++)
    {
      double u = log(gsl_rng_uniform_pos (r)) / alpha[i];
      
      theta[i] = u;

      if (u > umax || i == 0) {
        umax = u;
      }
    }
  
  for (i = 0; i < K; i++)
    {
      theta[i] = exp(theta[i] - umax);
    }
  
  for (i = 0; i < K; i++)
    {
      theta[i] = theta[i] * gsl_ran_gamma (r, alpha[i] + 1.0, 1.0);
    }

  for (i = 0; i < K; i++)
    {
      norm += theta[i];
    }

  for (i = 0; i < K; i++)
    {
      theta[i] /= norm;
    }
}

void
ran_dirichlet (const gsl_rng * r, const size_t K,
                   const double alpha[], double theta[])
{
	size_t i;
    double norm = 0.0;
	for (i = 0; i < K; i++)
    {
      theta[i] = gsl_ran_gamma (r, alpha[i], 1.0); // First generation of gamma distribution of size K
    }
	for (i = 0; i < K; i++)
    {
      norm += theta[i];
    }

	if (norm < GSL_SQRT_DBL_MIN)  /* Handle underflow */
	{
	   ran_dirichlet_small (r, K, alpha, theta);
	   return;
	}

	for (i = 0; i < K; i++)
	{
	  theta[i] /= norm;
	}

}

// Example usage to produce n values under the dirichlet distribution
/*int main()
{
	
	double alpha[2] = { 1, 1};
    double theta[2] = { 1, 1};
    gsl_rng * r;    
    r=gsl_rng_alloc(gsl_rng_mt19937);
    // Creating always a different seed
    struct timeval tv; 
    gettimeofday(&tv,0);
    unsigned long mySeed = tv.tv_sec + tv.tv_usec;
    gsl_rng_set(r, mySeed);
    ran_dirichlet(r, 2,alpha,theta);
    printf("thta values %f, %f sum: %f\n", theta[0], theta[1], theta[0] + theta[1]);
    gsl_rng_free(r);
    return 0;
}*/
