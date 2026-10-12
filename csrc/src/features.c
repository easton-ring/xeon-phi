/* Modulation-classification features; formulas in docs/features.md. */
#include <math.h>
#include <stdlib.h>

#include "knc/features.h"

#define TWO_PI 6.283185307179586476925286766559

typedef long kidx;

static const char *const fnames[KNC_NUM_FEATURES] = {
    "power_db", "power_var_norm", "amp_mean", "amp_m4", "sigma_aa",
    "c20_abs", "c40_abs", "c41_abs", "c42",
    "m2_spec_peak", "m4_spec_peak", "dphi_std", "dphi_kurt",
    "spec_centroid", "spec_spread", "spec_kurt", "spec_flatness"};

const char *knc_feature_name(int i)
{
    return (i >= 0 && i < KNC_NUM_FEATURES) ? fnames[i] : "?";
}

/* max_k |FFT(z)[k]| / n where z is zero-padded to plan->n */
static double spec_peak(const knc_fft_plan *p, double *z, size_t n)
{
    size_t k;
    double best = 0.0;
    knc_fft_f64(p, z, 0);
    for (k = 0; k < p->n; k++) {
        double m = sqrt(z[2 * k] * z[2 * k] + z[2 * k + 1] * z[2 * k + 1]);
        if (m > best || m != m)
            best = m;
    }
    return best / (double)n;
}

int knc_features(const knc_fft_plan *p, const float *x, size_t n, double *work, double *f)
{
    size_t i, nf;
    double P = 0, pv = 0, mr = 0, mi = 0, pc = 0, sc, am = 0, a4 = 0, saa = 0;
    double m20r = 0, m20i = 0, m40r = 0, m40i = 0, m41r = 0, m41i = 0, c20r, c20i, t;
    double dm = 0, d2 = 0, d4 = 0, tot = 0, cen = 0, spr = 0, sk = 0, lg = 0;
    double *y = work, *z = work + 2 * p->n, *pw = work + 4 * p->n;

    if (!p || !x || !work || !f || n < 2 || p->n < n || p->n >= 2 * n)
        return KNC_EINVAL;
    nf = p->n;
    for (i = 0; i < n; i++) {
        double re = x[2 * i], im = x[2 * i + 1];
        P += re * re + im * im;
        mr += re;
        mi += im;
    }
    P /= (double)n;
    mr /= (double)n;
    mi /= (double)n;
    for (i = 0; i < n; i++) {
        double re = x[2 * i], im = x[2 * i + 1], e = re * re + im * im - P;
        pv += e * e;
    }
    f[0] = 10.0 * log10(P + 1e-30);
    f[1] = (pv / (double)n) / (P * P + 1e-300);

    /* y = (x - mean) / sqrt(Pc) */
    for (i = 0; i < n; i++) {
        double re = x[2 * i] - mr, im = x[2 * i + 1] - mi;
        y[2 * i] = re;
        y[2 * i + 1] = im;
        pc += re * re + im * im;
    }
    pc /= (double)n;
    sc = 1.0 / sqrt(pc + 1e-300);
    for (i = 0; i < 2 * n; i++)
        y[i] *= sc;

    for (i = 0; i < n; i++) {
        double re = y[2 * i], im = y[2 * i + 1], a2 = re * re + im * im;
        double y2r = re * re - im * im, y2i = 2 * re * im;
        am += sqrt(a2);
        a4 += a2 * a2;
        m20r += y2r;
        m20i += y2i;
        m40r += y2r * y2r - y2i * y2i;
        m40i += 2 * y2r * y2i;
        m41r += y2r * a2; /* y^3 conj(y) = y^2 |y|^2 */
        m41i += y2i * a2;
    }
    am /= (double)n;
    a4 /= (double)n;
    m20r /= (double)n; m20i /= (double)n;
    m40r /= (double)n; m40i /= (double)n;
    m41r /= (double)n; m41i /= (double)n;
    for (i = 0; i < n; i++) {
        double a = sqrt(y[2 * i] * y[2 * i] + y[2 * i + 1] * y[2 * i + 1]);
        t = a / (am + 1e-300) - 1.0;
        saa += t * t;
    }
    f[2] = am;
    f[3] = a4;
    f[4] = sqrt(saa / (double)n);
    /* M21 = mean|y|^2 = 1 by construction; kept symbolic for clarity. */
    c20r = m20r;
    c20i = m20i;
    f[5] = sqrt(c20r * c20r + c20i * c20i);
    {
        double s2r = c20r * c20r - c20i * c20i, s2i = 2 * c20r * c20i; /* M20^2 */
        double c40r = m40r - 3 * s2r, c40i = m40i - 3 * s2i;
        double c41r = m41r - 3 * c20r * 1.0, c41i = m41i - 3 * c20i * 1.0;
        f[6] = sqrt(c40r * c40r + c40i * c40i);
        f[7] = sqrt(c41r * c41r + c41i * c41i);
        f[8] = a4 - (c20r * c20r + c20i * c20i) - 2.0;
    }

    /* Spectral peaks of y^2 and y^4 (zero-padded to nf). */
    for (i = 0; i < 2 * nf; i++)
        z[i] = 0.0;
    for (i = 0; i < n; i++) {
        double re = y[2 * i], im = y[2 * i + 1];
        z[2 * i] = re * re - im * im;
        z[2 * i + 1] = 2 * re * im;
    }
    f[9] = spec_peak(p, z, n);
    for (i = 0; i < 2 * nf; i++)
        z[i] = 0.0;
    for (i = 0; i < n; i++) {
        double re = y[2 * i] * y[2 * i] - y[2 * i + 1] * y[2 * i + 1];
        double im = 2 * y[2 * i] * y[2 * i + 1];
        z[2 * i] = re * re - im * im;
        z[2 * i + 1] = 2 * re * im;
    }
    f[10] = spec_peak(p, z, n);

    /* Instantaneous phase difference d[i] = arg(x[i+1] conj(x[i])). */
    for (i = 0; i + 1 < n; i++) {
        double a = x[2 * i + 2], b = x[2 * i + 3], c = x[2 * i], d = x[2 * i + 1];
        pw[i] = atan2(b * c - a * d, a * c + b * d);
        dm += pw[i];
    }
    dm /= (double)(n - 1);
    for (i = 0; i + 1 < n; i++) {
        t = (pw[i] - dm) * (pw[i] - dm);
        d2 += t;
        d4 += t * t;
    }
    d2 /= (double)(n - 1);
    d4 /= (double)(n - 1);
    f[11] = sqrt(d2);
    f[12] = d2 > 0 ? d4 / (d2 * d2) : 0.0;

    /* Hann(n)-windowed periodogram of raw x, zero-padded to nf. */
    for (i = 0; i < 2 * nf; i++)
        z[i] = 0.0;
    for (i = 0; i < n; i++) {
        double w = 0.5 - 0.5 * cos(TWO_PI * (double)i / (double)n);
        z[2 * i] = w * x[2 * i];
        z[2 * i + 1] = w * x[2 * i + 1];
    }
    knc_fft_f64(p, z, 0);
    for (i = 0; i < nf; i++) {
        pw[i] = z[2 * i] * z[2 * i] + z[2 * i + 1] * z[2 * i + 1];
        tot += pw[i];
    }
    for (i = 0; i < nf; i++) {
        double fr = (i < nf / 2) ? (double)i / (double)nf : (double)i / (double)nf - 1.0;
        cen += fr * pw[i];
    }
    cen /= (tot + 1e-300);
    for (i = 0; i < nf; i++) {
        double fr = (i < nf / 2) ? (double)i / (double)nf : (double)i / (double)nf - 1.0;
        t = (fr - cen) * (fr - cen);
        spr += t * pw[i];
        sk += t * t * pw[i];
    }
    spr /= (tot + 1e-300);
    sk /= (tot + 1e-300);
    f[13] = cen;
    f[14] = sqrt(spr);
    f[15] = spr > 0 ? sk / (spr * spr) : 0.0;
    t = tot / (double)nf;
    for (i = 0; i < nf; i++)
        lg += log(pw[i] + 1e-12 * t + 1e-300);
    f[16] = exp(lg / (double)nf) / (t + 1e-300);
    return KNC_OK;
}

int knc_features_batch(const float *x, size_t batch, size_t n, double *feat)
{
    knc_fft_plan plan;
    size_t nf = 1;
    kidx b, nb = (kidx)batch;
    int bad = 0;

    if (n < 2 || (batch > 0 && (!x || !feat)))
        return KNC_EINVAL;
    while (nf < n)
        nf <<= 1;
    if (knc_fft_plan_init(&plan, nf) != KNC_OK)
        return 2;
#pragma omp parallel reduction(|:bad)
    {
        double *work = (double *)malloc(6 * nf * sizeof(double));
        if (!work)
            bad |= 2;
        /* Every thread must reach the worksharing loop, even without scratch. */
#pragma omp for schedule(static)
        for (b = 0; b < nb; b++)
            if (work)
                bad |= knc_features(&plan, x + 2 * (size_t)b * n, n, work,
                                    feat + (size_t)b * KNC_NUM_FEATURES);
        free(work);
    }
    knc_fft_plan_free(&plan);
    return bad ? 2 : KNC_OK;
}
