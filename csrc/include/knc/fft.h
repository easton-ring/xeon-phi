#ifndef KNC_FFT_H
#define KNC_FFT_H
/* Iterative radix-2 FFT (power-of-two n) and batched periodogram.
 * Conventions match numpy.fft:
 *   forward  X[k] = sum_n x[n] exp(-2*pi*i*k*n/N)     (unnormalized)
 *   inverse  x[n] = (1/N) sum_k X[k] exp(+2*pi*i*k*n/N) */
#include <stddef.h>

#include "knc/kernels.h"

#define KNC_WIN_RECT 0
#define KNC_WIN_HANN 1 /* periodic Hann: w[n] = 0.5 - 0.5*cos(2*pi*n/N) */

typedef struct {
    size_t n;
    unsigned log2n;
    size_t *bitrev;  /* n entries */
    double *tw64;    /* n/2 complex twiddles exp(-2*pi*i*k/n), interleaved */
    float *tw32;     /* same, rounded to float */
    double *hann64;  /* n periodic Hann coefficients */
    float *hann32;
    double hann_sumsq; /* sum of w^2 in double */
} knc_fft_plan;

/* n must be a power of two >= 1. Returns KNC_OK, KNC_EINVAL or 2 (out of memory). */
int knc_fft_plan_init(knc_fft_plan *p, size_t n);
void knc_fft_plan_free(knc_fft_plan *p);

/* In-place transform of one n-point complex vector. inverse != 0 -> inverse (scaled 1/n). */
int knc_fft_f32(const knc_fft_plan *p, float *x, int inverse);
int knc_fft_f64(const knc_fft_plan *p, double *x, int inverse);
/* In-place forward transforms of batch contiguous vectors (OpenMP over batch). */
int knc_fft_batch_f32(const knc_fft_plan *p, float *x, size_t batch);
int knc_fft_batch_f64(const knc_fft_plan *p, double *x, size_t batch);

/* Periodogram of batch examples of n = p->n samples:
 *   P[b][k] = |FFT(w . x_b)[k]|^2 / sum(w^2)
 * (w = 1 gives |X|^2 / N). The Hann window requires n >= 2. work holds batch*n complex values (scratch);
 * out holds batch*n reals. */
int knc_psd_f32(const knc_fft_plan *p, const float *x, size_t batch, int window, float *work, float *out);
int knc_psd_f64(const knc_fft_plan *p, const double *x, size_t batch, int window, double *work, double *out);

#endif
