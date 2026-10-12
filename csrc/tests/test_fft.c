/* FFT and periodogram tests against an O(N^2) double DFT. */
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "knc/fft.h"

#define TWO_PI 6.283185307179586476925286766559

static void dft(const double *x, double *X, size_t n)
{
    size_t k, j;
    for (k = 0; k < n; k++) {
        double re = 0, im = 0;
        for (j = 0; j < n; j++) {
            double a = -TWO_PI * (double)((k * j) % n) / (double)n, c = cos(a), s = sin(a);
            re += x[2 * j] * c - x[2 * j + 1] * s;
            im += x[2 * j] * s + x[2 * j + 1] * c;
        }
        X[2 * k] = re;
        X[2 * k + 1] = im;
    }
}

/* ||a - b||_2 / ||b||_2 */
static double relerr32(const float *a, const double *b, size_t m)
{
    double e = 0, r = 0;
    size_t i;
    for (i = 0; i < m; i++) {
        e += (a[i] - b[i]) * (a[i] - b[i]);
        r += b[i] * b[i];
    }
    return sqrt(e / (r > 0 ? r : 1));
}
static double relerr64(const double *a, const double *b, size_t m)
{
    double e = 0, r = 0;
    size_t i;
    for (i = 0; i < m; i++) {
        e += (a[i] - b[i]) * (a[i] - b[i]);
        r += b[i] * b[i];
    }
    return sqrt(e / (r > 0 ? r : 1));
}

static void test_size(size_t n, int off)
{
    knc_fft_plan p;
    float *xf = (float *)malloc((2 * n + 2) * sizeof(float)) , *x = xf + off;
    double *xdb = (double *)malloc((2 * n + 2) * sizeof(double)), *xd = xdb + off;
    double *in = (double *)malloc(2 * n * sizeof(double)), *X = (double *)malloc(2 * n * sizeof(double));
    double *ref = (double *)malloc(n * sizeof(double));
    float *work = (float *)malloc(2 * n * sizeof(float)), *P = (float *)malloc(n * sizeof(float));
    double lg = 1;
    size_t i, k;
    unsigned b;

    CHECK(knc_fft_plan_init(&p, n) == KNC_OK);
    for (b = 0; ((size_t)1 << b) < n; b++)
        ;
    if (b > 1)
        lg = b;
    for (i = 0; i < 2 * n; i++)
        xd[i] = in[i] = x[i] = (float)urand();
    dft(in, X, n);
    CHECK(knc_fft_f32(&p, x, 0) == KNC_OK);
    CHECK(knc_fft_f64(&p, xd, 0) == KNC_OK);
    CHECK(relerr32(x, X, 2 * n) <= 5 * lg * FLT_EPSILON);
    CHECK(relerr64(xd, X, 2 * n) <= 5 * lg * DBL_EPSILON + 1e-14 * (n > 256));
    /* inverse round trip */
    CHECK(knc_fft_f32(&p, x, 1) == KNC_OK);
    CHECK(knc_fft_f64(&p, xd, 1) == KNC_OK);
    CHECK(relerr32(x, in, 2 * n) <= 10 * lg * FLT_EPSILON);
    CHECK(relerr64(xd, in, 2 * n) <= 10 * lg * DBL_EPSILON + 1e-14);

    /* periodogram, rect and Hann */
    for (i = 0; i < 2 * n; i++)
        x[i] = (float)in[i];
    {
        double sumsq = 0, tot = 0, ptot = 0;
        CHECK(knc_psd_f32(&p, x, 1, KNC_WIN_RECT, work, P) == KNC_OK);
        for (k = 0; k < n; k++) {
            ref[k] = (X[2 * k] * X[2 * k] + X[2 * k + 1] * X[2 * k + 1]) / (double)n;
            tot += ref[k];
            ptot += P[k];
        }
        for (i = 0; i < 2 * n; i++)
            sumsq += in[i] * in[i];
        CHECK(fabs(tot - sumsq) <= 1e-9 * sumsq); /* Parseval for the reference */
        CHECK(fabs(ptot - sumsq) <= 20 * lg * FLT_EPSILON * sumsq);
        {
            double e = 0, r = 0;
            for (k = 0; k < n; k++) {
                e += (P[k] - ref[k]) * (P[k] - ref[k]);
                r += ref[k] * ref[k];
            }
            CHECK(sqrt(e / r) <= 15 * lg * FLT_EPSILON);
        }
        /* Hann */
        for (i = 0; i < n; i++) {
            double w = 0.5 - 0.5 * cos(TWO_PI * (double)i / (double)n);
            in[2 * i] *= w;
            in[2 * i + 1] *= w;
        }
        dft(in, X, n);
        CHECK(knc_psd_f32(&p, x, 1, KNC_WIN_HANN, work, P) == (n > 1 ? KNC_OK : KNC_EINVAL));
        if (n > 1) {
            double e = 0, r = 0;
            for (k = 0; k < n; k++) {
                double rk = (X[2 * k] * X[2 * k] + X[2 * k + 1] * X[2 * k + 1]) / p.hann_sumsq;
                e += (P[k] - rk) * (P[k] - rk);
                r += rk * rk;
            }
            CHECK(sqrt(e / r) <= 15 * lg * FLT_EPSILON);
        }
    }
    knc_fft_plan_free(&p);
    free(xf); free(xdb); free(in); free(X); free(ref); free(work); free(P);
}

int main(void)
{
    size_t n;
    knc_fft_plan p;
    float x[64], work[64], P[32];
    int i;

    for (n = 1; n <= 4096; n <<= 1) {
        test_size(n, 0);
        test_size(n, 1); /* misaligned by one float */
    }
    /* impulse -> all ones, exactly */
    CHECK(knc_fft_plan_init(&p, 32) == KNC_OK);
    memset(x, 0, sizeof x);
    x[0] = 1;
    CHECK(knc_fft_f32(&p, x, 0) == KNC_OK);
    for (i = 0; i < 32; i++)
        CHECK(x[2 * i] == 1.0f && x[2 * i + 1] == 0.0f);
    /* zeros -> zeros */
    memset(x, 0, sizeof x);
    CHECK(knc_fft_f32(&p, x, 0) == KNC_OK && x[10] == 0.0f);
    /* NaN in one input contaminates every output bin */
    x[6] = (float)NAN;
    CHECK(knc_fft_f32(&p, x, 0) == KNC_OK);
    for (i = 0; i < 32; i++)
        CHECK(isnan(x[2 * i]) || isnan(x[2 * i + 1]));
    memset(x, 0, sizeof x);
    x[0] = (float)INFINITY;
    CHECK(knc_psd_f32(&p, x, 1, KNC_WIN_RECT, work, P) == KNC_OK && !isfinite(P[3])); /* inf*0 -> NaN in butterflies */
    CHECK(knc_psd_f32(&p, x, 1, 7, work, P) == KNC_EINVAL);
    knc_fft_plan_free(&p);
    /* invalid sizes */
    CHECK(knc_fft_plan_init(&p, 0) == KNC_EINVAL);
    CHECK(knc_fft_plan_init(&p, 3) == KNC_EINVAL);
    CHECK(knc_fft_plan_init(&p, 96) == KNC_EINVAL);
    CHECK(knc_fft_f32(&p, x, 0) == KNC_EINVAL); /* zeroed plan */
    return TEST_EXIT();
}
