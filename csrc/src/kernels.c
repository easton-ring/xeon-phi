/* Portable C99 DSP kernels. Plain loops; OpenMP pragmas are ignored when
 * OpenMP is disabled. Build without -ffast-math and with -ffp-contract=off
 * (NaN/Inf propagation and reproducible rounding, docs/kernels.md). */
#include <math.h>

#include "knc/kernels.h"

#define TWO_PI 6.283185307179586476925286766559

/* OpenMP loop indices are signed for compatibility with older runtimes. */
typedef long kidx;

#define DEFINE_ELEMENTWISE(T, SUF)                                              \
    int knc_cmul_##SUF(const T *x, const T *y, T *z, size_t n)                  \
    {                                                                           \
        kidx i, nn = (kidx)n;                                                   \
        if (n > 0 && (!x || !y || !z))                                          \
            return KNC_EINVAL;                                                  \
        _Pragma("omp parallel for schedule(static) if(nn > 65536)")             \
        for (i = 0; i < nn; i++) {                                              \
            T a = x[2 * i], b = x[2 * i + 1], c = y[2 * i], d = y[2 * i + 1];   \
            z[2 * i] = a * c - b * d;                                           \
            z[2 * i + 1] = a * d + b * c;                                       \
        }                                                                       \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_magsq_##SUF(const T *x, T *out, size_t n)                           \
    {                                                                           \
        kidx i, nn = (kidx)n;                                                   \
        if (n > 0 && (!x || !out))                                              \
            return KNC_EINVAL;                                                  \
        _Pragma("omp parallel for schedule(static) if(nn > 65536)")             \
        for (i = 0; i < nn; i++)                                                \
            out[i] = x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1];         \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_mean_power_##SUF(const T *x, size_t batch, size_t n, T *out)        \
    {                                                                           \
        kidx b, nb = (kidx)batch;                                               \
        if (n == 0 || (batch > 0 && (!x || !out)))                              \
            return KNC_EINVAL;                                                  \
        _Pragma("omp parallel for schedule(static)")                            \
        for (b = 0; b < nb; b++) {                                              \
            const T *p = x + 2 * (size_t)b * n;                                 \
            T acc = 0;                                                          \
            size_t i;                                                           \
            for (i = 0; i < 2 * n; i++)                                         \
                acc += p[i] * p[i];                                             \
            out[b] = acc / (T)n;                                                \
        }                                                                       \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_xcorr_##SUF(const T *x, size_t n, const T *y, size_t l, T *r)       \
    {                                                                           \
        size_t k, m;                                                            \
        if (l == 0 || l > n || !x || !y || !r)                                  \
            return KNC_EINVAL;                                                  \
        for (k = 0; k + l <= n; k++) {                                          \
            T re = 0, im = 0;                                                   \
            const T *xp = x + 2 * k;                                            \
            for (m = 0; m < l; m++) {                                           \
                T a = xp[2 * m], b = xp[2 * m + 1], c = y[2 * m], d = y[2 * m + 1]; \
                re += a * c + b * d;                                            \
                im += b * c - a * d;                                            \
            }                                                                   \
            r[2 * k] = re;                                                      \
            r[2 * k + 1] = im;                                                  \
        }                                                                       \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_xcorr_batch_##SUF(const T *x, size_t batch, size_t n, const T *y,   \
                              size_t l, T *r)                                   \
    {                                                                           \
        kidx b, nb = (kidx)batch;                                               \
        size_t nout;                                                            \
        int bad = 0;                                                            \
        if (l == 0 || l > n || !y || (batch > 0 && (!x || !r)))                 \
            return KNC_EINVAL;                                                  \
        nout = n - l + 1;                                                       \
        _Pragma("omp parallel for schedule(static) reduction(|:bad)")           \
        for (b = 0; b < nb; b++)                                                \
            bad |= knc_xcorr_##SUF(x + 2 * (size_t)b * n, n, y, l,              \
                                   r + 2 * (size_t)b * nout);                   \
        return bad ? KNC_EINVAL : KNC_OK;                                       \
    }                                                                           \
    int knc_cfo_##SUF(const T *x, size_t batch, size_t n, T *out)               \
    {                                                                           \
        kidx b, nb = (kidx)batch;                                               \
        if (n < 2 || (batch > 0 && (!x || !out)))                               \
            return KNC_EINVAL;                                                  \
        _Pragma("omp parallel for schedule(static)")                            \
        for (b = 0; b < nb; b++) {                                              \
            const T *p = x + 2 * (size_t)b * n;                                 \
            T re = 0, im = 0;                                                   \
            size_t i;                                                           \
            for (i = 0; i + 1 < n; i++) {                                       \
                T a = p[2 * i + 2], bb = p[2 * i + 3], c = p[2 * i], d = p[2 * i + 1]; \
                re += a * c + bb * d;                                           \
                im += bb * c - a * d;                                           \
            }                                                                   \
            out[b] = (T)(atan2((double)im, (double)re) / TWO_PI);               \
        }                                                                       \
        return KNC_OK;                                                          \
    }

DEFINE_ELEMENTWISE(float, f32)
DEFINE_ELEMENTWISE(double, f64)
