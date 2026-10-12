/* Iterative radix-2 decimation-in-time FFT and batched periodogram (C99). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "knc/fft.h"

#define TWO_PI 6.283185307179586476925286766559

typedef long kidx;

int knc_fft_plan_init(knc_fft_plan *p, size_t n)
{
    size_t i, k;
    unsigned lg = 0;
    memset(p, 0, sizeof *p);
    if (n == 0 || (n & (n - 1)) != 0)
        return KNC_EINVAL;
    while (((size_t)1 << lg) < n)
        lg++;
    p->n = n;
    p->log2n = lg;
    p->bitrev = (size_t *)malloc(n * sizeof(size_t));
    p->tw64 = (double *)malloc((n / 2 + 1) * 2 * sizeof(double));
    p->tw32 = (float *)malloc((n / 2 + 1) * 2 * sizeof(float));
    p->hann64 = (double *)malloc(n * sizeof(double));
    p->hann32 = (float *)malloc(n * sizeof(float));
    if (!p->bitrev || !p->tw64 || !p->tw32 || !p->hann64 || !p->hann32) {
        knc_fft_plan_free(p);
        return 2;
    }
    for (i = 0; i < n; i++) {
        size_t r = 0, v = i;
        for (k = 0; k < lg; k++) {
            r = (r << 1) | (v & 1);
            v >>= 1;
        }
        p->bitrev[i] = r;
    }
    for (k = 0; k < n / 2; k++) {
        double a = -TWO_PI * (double)k / (double)n;
        p->tw64[2 * k] = cos(a);
        p->tw64[2 * k + 1] = sin(a);
        p->tw32[2 * k] = (float)p->tw64[2 * k];
        p->tw32[2 * k + 1] = (float)p->tw64[2 * k + 1];
    }
    p->hann_sumsq = 0.0;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(TWO_PI * (double)i / (double)n);
        p->hann64[i] = w;
        p->hann32[i] = (float)w;
        p->hann_sumsq += w * w;
    }
    return KNC_OK;
}

void knc_fft_plan_free(knc_fft_plan *p)
{
    free(p->bitrev);
    free(p->tw64);
    free(p->tw32);
    free(p->hann64);
    free(p->hann32);
    memset(p, 0, sizeof *p);
}

#define DEFINE_FFT(T, SUF, TW, HANN)                                            \
    int knc_fft_##SUF(const knc_fft_plan *p, T *x, int inverse)                 \
    {                                                                           \
        size_t n, i, len, half, step, j, s;                                     \
        if (!p || !x || p->n == 0)                                              \
            return KNC_EINVAL;                                                  \
        n = p->n;                                                               \
        for (i = 0; i < n; i++) {                                               \
            size_t r = p->bitrev[i];                                            \
            if (r > i) {                                                        \
                T tr = x[2 * i], ti = x[2 * i + 1];                             \
                x[2 * i] = x[2 * r];                                            \
                x[2 * i + 1] = x[2 * r + 1];                                    \
                x[2 * r] = tr;                                                  \
                x[2 * r + 1] = ti;                                              \
            }                                                                   \
        }                                                                       \
        for (len = 2; len <= n; len <<= 1) {                                    \
            half = len >> 1;                                                    \
            step = n / len;                                                     \
            for (s = 0; s < n; s += len) {                                      \
                T *a = x + 2 * s, *b = x + 2 * (s + half);                      \
                for (j = 0; j < half; j++) {                                    \
                    T wr = p->TW[2 * j * step];                                 \
                    T wi = inverse ? -p->TW[2 * j * step + 1]                   \
                                   : p->TW[2 * j * step + 1];                   \
                    T br = b[2 * j] * wr - b[2 * j + 1] * wi;                   \
                    T bi = b[2 * j] * wi + b[2 * j + 1] * wr;                   \
                    T ar = a[2 * j], ai = a[2 * j + 1];                         \
                    a[2 * j] = ar + br;                                         \
                    a[2 * j + 1] = ai + bi;                                     \
                    b[2 * j] = ar - br;                                         \
                    b[2 * j + 1] = ai - bi;                                     \
                }                                                               \
            }                                                                   \
        }                                                                       \
        if (inverse) {                                                          \
            T sc = (T)1 / (T)n;                                                 \
            for (i = 0; i < 2 * n; i++)                                         \
                x[i] *= sc;                                                     \
        }                                                                       \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_fft_batch_##SUF(const knc_fft_plan *p, T *x, size_t batch)          \
    {                                                                           \
        kidx b, nb = (kidx)batch;                                               \
        if (!p || p->n == 0 || (batch > 0 && !x))                               \
            return KNC_EINVAL;                                                  \
        _Pragma("omp parallel for schedule(static)")                            \
        for (b = 0; b < nb; b++)                                                \
            knc_fft_##SUF(p, x + 2 * (size_t)b * p->n, 0);                      \
        return KNC_OK;                                                          \
    }                                                                           \
    int knc_psd_##SUF(const knc_fft_plan *p, const T *x, size_t batch,          \
                      int window, T *work, T *out)                              \
    {                                                                           \
        kidx b, nb = (kidx)batch;                                               \
        T norm;                                                                 \
        if (!p || p->n == 0 || (window != KNC_WIN_RECT && window != KNC_WIN_HANN) \
            || (window == KNC_WIN_HANN && p->n < 2)                             \
            || (batch > 0 && (!x || !work || !out)))                            \
            return KNC_EINVAL;                                                  \
        norm = (T)(window == KNC_WIN_HANN ? 1.0 / p->hann_sumsq : 1.0 / (double)p->n); \
        _Pragma("omp parallel for schedule(static)")                            \
        for (b = 0; b < nb; b++) {                                              \
            size_t n = p->n, i;                                                 \
            const T *xb = x + 2 * (size_t)b * n;                                \
            T *wb = work + 2 * (size_t)b * n, *ob = out + (size_t)b * n;        \
            if (window == KNC_WIN_HANN) {                                       \
                for (i = 0; i < n; i++) {                                       \
                    wb[2 * i] = xb[2 * i] * p->HANN[i];                         \
                    wb[2 * i + 1] = xb[2 * i + 1] * p->HANN[i];                 \
                }                                                               \
            } else {                                                            \
                memcpy(wb, xb, 2 * n * sizeof(T));                              \
            }                                                                   \
            knc_fft_##SUF(p, wb, 0);                                            \
            for (i = 0; i < n; i++)                                             \
                ob[i] = (wb[2 * i] * wb[2 * i] + wb[2 * i + 1] * wb[2 * i + 1]) * norm; \
        }                                                                       \
        return KNC_OK;                                                          \
    }

DEFINE_FFT(float, f32, tw32, hann32)
DEFINE_FFT(double, f64, tw64, hann64)
