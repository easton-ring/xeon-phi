/* Kernel tests against double-precision scalar references. Error bounds are
 * the standard rounding-error bounds documented in docs/kernels.md. */
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "knc/kernels.h"

#define MAXN 1100
static const size_t sizes[] = {1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 1024, 1025};
#define NSIZES (sizeof sizes / sizeof sizes[0])

static float xs[2 * MAXN + 8], ys[2 * MAXN + 8], zs[2 * MAXN + 8];
static double xd[2 * MAXN + 8], yd[2 * MAXN + 8], zd[2 * MAXN + 8];

static void fill(size_t n2)
{
    size_t i;
    for (i = 0; i < n2; i++) {
        xd[i] = (double)(xs[i] = (float)urand());
        yd[i] = (double)(ys[i] = (float)urand());
    }
}

static void test_elementwise(size_t n, int off)
{
    float *x = xs + off, *y = ys + off, *z = zs + off;
    double *xx = xd + off, *yy = yd + off, *zz = zd + off;
    size_t i;
    fill(2 * n + 2);
    CHECK(knc_cmul_f32(x, y, z, n) == KNC_OK);
    CHECK(knc_cmul_f64(xx, yy, zz, n) == KNC_OK);
    for (i = 0; i < n; i++) {
        double a = x[2 * i], b = x[2 * i + 1], c = y[2 * i], d = y[2 * i + 1];
        double mag = fabs(a * c) + fabs(b * d) + fabs(a * d) + fabs(b * c);
        CHECK(fabs(z[2 * i] - (a * c - b * d)) <= 3 * FLT_EPSILON * mag);
        CHECK(fabs(z[2 * i + 1] - (a * d + b * c)) <= 3 * FLT_EPSILON * mag);
        CHECK(fabs(zz[2 * i] - (a * c - b * d)) <= 3 * DBL_EPSILON * mag);
    }
    CHECK(knc_magsq_f32(x, z, n) == KNC_OK);
    CHECK(knc_magsq_f64(xx, zz, n) == KNC_OK);
    for (i = 0; i < n; i++) {
        double r = (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        CHECK(fabs(z[i] - r) <= 2 * FLT_EPSILON * r);
        CHECK(fabs(zz[i] - r) <= 2 * DBL_EPSILON * r);
    }
    {
        float mp;
        double mpd, r = 0;
        for (i = 0; i < 2 * n; i++)
            r += (double)x[i] * x[i];
        r /= (double)n;
        CHECK(knc_mean_power_f32(x, 1, n, &mp) == KNC_OK);
        CHECK(knc_mean_power_f64(xx, 1, n, &mpd) == KNC_OK);
        CHECK(fabs(mp - r) <= (2.0 * n + 2) * FLT_EPSILON * r);
        CHECK(fabs(mpd - r) <= (2.0 * n + 2) * DBL_EPSILON * r);
    }
    if (n >= 2) {
        float f;
        double fd, re = 0, im = 0, mag = 0, ref;
        for (i = 0; i + 1 < n; i++) {
            double a = x[2 * i + 2], b = x[2 * i + 3], c = x[2 * i], d = x[2 * i + 1];
            re += a * c + b * d;
            im += b * c - a * d;
            mag += (fabs(a) + fabs(b)) * (fabs(c) + fabs(d));
        }
        ref = atan2(im, re) / 6.283185307179586;
        CHECK(knc_cfo_f32(x, 1, n, &f) == KNC_OK);
        CHECK(knc_cfo_f64(xx, 1, n, &fd) == KNC_OK);
        /* |d angle| <= |d r| / |r|; skip the ill-conditioned |r| ~ 0 case */
        if (sqrt(re * re + im * im) > 1e-3 * mag) {
            double bound = (2.0 * n + 4) * FLT_EPSILON * mag / sqrt(re * re + im * im) / 6.28 + 1e-7;
            double e = fabs(f - ref);
            if (e > 0.5) e = 1.0 - e; /* wrap at +-0.5 */
            CHECK(e <= bound);
            CHECK(fabs(fd - ref) <= 1e-12);
        }
    }
}

static void test_xcorr(size_t n, size_t l, int off)
{
    float *x = xs + off, *y = ys + off, *r = zs + off;
    double *rd = zd + off;
    size_t k, m;
    fill(2 * n + 2);
    CHECK(knc_xcorr_f32(x, n, y, l, r) == KNC_OK);
    CHECK(knc_xcorr_f64(xd + off, n, yd + off, l, rd) == KNC_OK);
    for (k = 0; k + l <= n; k++) {
        double re = 0, im = 0, mag = 0;
        for (m = 0; m < l; m++) {
            double a = x[2 * (k + m)], b = x[2 * (k + m) + 1], c = y[2 * m], d = y[2 * m + 1];
            re += a * c + b * d;
            im += b * c - a * d;
            mag += (fabs(a) + fabs(b)) * (fabs(c) + fabs(d));
        }
        CHECK(fabs(r[2 * k] - re) <= (l + 2.0) * FLT_EPSILON * mag);
        CHECK(fabs(r[2 * k + 1] - im) <= (l + 2.0) * FLT_EPSILON * mag);
        CHECK(fabs(rd[2 * k] - re) <= (l + 2.0) * DBL_EPSILON * mag * 2);
    }
}

int main(void)
{
    size_t s;
    int off;
    float o[8], nanf_ = (float)NAN, inff = (float)INFINITY;

    for (s = 0; s < NSIZES; s++)
        for (off = 0; off < 3; off++) /* off = 1: 4-byte misaligned base */
            test_elementwise(sizes[s], off);
    for (off = 0; off < 2; off++) {
        test_xcorr(1, 1, off);
        test_xcorr(17, 16, off);
        test_xcorr(64, 8, off);
        test_xcorr(1025, 64, off);
    }

    /* zeros */
    memset(xs, 0, sizeof xs);
    CHECK(knc_magsq_f32(xs, zs, 16) == KNC_OK && zs[0] == 0 && zs[15] == 0);
    CHECK(knc_mean_power_f32(xs, 2, 8, o) == KNC_OK && o[0] == 0 && o[1] == 0);
    CHECK(knc_cfo_f32(xs, 1, 16, o) == KNC_OK && o[0] == 0);
    CHECK(knc_xcorr_f32(xs, 16, xs, 4, zs) == KNC_OK && zs[0] == 0);

    /* NaN / Inf propagation (requires no -ffast-math) */
    xs[2] = nanf_;
    CHECK(knc_magsq_f32(xs, zs, 4) == KNC_OK && isnan(zs[1]) && zs[0] == 0);
    CHECK(knc_mean_power_f32(xs, 2, 2, o) == KNC_OK && isnan(o[0]) && o[1] == 0);
    CHECK(knc_cmul_f32(xs, ys, zs, 4) == KNC_OK && isnan(zs[2]) && isnan(zs[3]));
    CHECK(knc_xcorr_f32(xs, 4, ys, 2, zs) == KNC_OK && isnan(zs[0]) && isnan(zs[2]));
    CHECK(knc_cfo_f32(xs, 1, 4, o) == KNC_OK && isnan(o[0]));
    xs[2] = inff;
    CHECK(knc_mean_power_f32(xs, 1, 4, o) == KNC_OK && isinf(o[0]));
    CHECK(knc_magsq_f32(xs, zs, 4) == KNC_OK && isinf(zs[1]));

    /* invalid sizes / pointers */
    CHECK(knc_cmul_f32(xs, ys, zs, 0) == KNC_OK);
    CHECK(knc_cmul_f32(NULL, ys, zs, 4) == KNC_EINVAL);
    CHECK(knc_magsq_f32(xs, NULL, 4) == KNC_EINVAL);
    CHECK(knc_mean_power_f32(xs, 1, 0, o) == KNC_EINVAL);
    CHECK(knc_mean_power_f32(NULL, 1, 4, o) == KNC_EINVAL);
    CHECK(knc_xcorr_f32(xs, 4, ys, 5, zs) == KNC_EINVAL);
    CHECK(knc_xcorr_f32(xs, 4, ys, 0, zs) == KNC_EINVAL);
    CHECK(knc_xcorr_batch_f32(xs, 2, 4, ys, 5, zs) == KNC_EINVAL);
    CHECK(knc_cfo_f32(xs, 1, 1, o) == KNC_EINVAL);
    return TEST_EXIT();
}
