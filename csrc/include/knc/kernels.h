#ifndef KNC_KERNELS_H
#define KNC_KERNELS_H
/* Portable C99 DSP kernels. Complex data is interleaved re/im (I/Q) arrays;
 * n counts complex samples. All functions return KNC_OK or KNC_EINVAL.
 * Exact definitions and error bounds: docs/kernels.md. */
#include <stddef.h>

#define KNC_OK 0
#define KNC_EINVAL 1

/* z[i] = x[i] * y[i]. n == 0 is a no-op. In-place (z == x or z == y) allowed. */
int knc_cmul_f32(const float *x, const float *y, float *z, size_t n);
int knc_cmul_f64(const double *x, const double *y, double *z, size_t n);

/* out[i] = re(x[i])^2 + im(x[i])^2. */
int knc_magsq_f32(const float *x, float *out, size_t n);
int knc_magsq_f64(const double *x, double *out, size_t n);

/* out[b] = (1/n) sum_i |x[b*n + i]|^2, b < batch. n must be > 0.
 * Accumulates in the input precision (same code on host and KNC). */
int knc_mean_power_f32(const float *x, size_t batch, size_t n, float *out);
int knc_mean_power_f64(const double *x, size_t batch, size_t n, double *out);

/* Valid-mode cross-correlation, same as numpy.correlate(x, y, "valid"):
 *   r[k] = sum_{m=0}^{l-1} x[k+m] * conj(y[m]),  k = 0 .. n-l.
 * Requires 0 < l <= n. Output has n-l+1 complex values. Direct O(n*l). */
int knc_xcorr_f32(const float *x, size_t n, const float *y, size_t l, float *r);
int knc_xcorr_f64(const double *x, size_t n, const double *y, size_t l, double *r);
/* Batched: x is batch*n, r is batch*(n-l+1); same template y for all. */
int knc_xcorr_batch_f32(const float *x, size_t batch, size_t n, const float *y, size_t l, float *r);
int knc_xcorr_batch_f64(const double *x, size_t batch, size_t n, const double *y, size_t l, double *r);

/* Frequency-offset estimate (cycles/sample) from the lag-1 autocorrelation:
 *   f = arg( sum_{i=0}^{n-2} x[i+1] * conj(x[i]) ) / (2*pi),  in (-0.5, 0.5].
 * Requires n >= 2. Batched over batch examples of n samples. */
int knc_cfo_f32(const float *x, size_t batch, size_t n, float *out);
int knc_cfo_f64(const double *x, size_t batch, size_t n, double *out);

#endif
