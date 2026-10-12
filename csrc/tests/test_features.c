/* Feature sanity tests (NumPy agreement is tested in tests/test_c_core.py). */
#include <stdlib.h>

#include "check.h"
#include "knc/features.h"

#define N 256

int main(void)
{
    static float x[2 * N * 3];
    static double f[3 * KNC_NUM_FEATURES], g[KNC_NUM_FEATURES], work[6 * N];
    knc_fft_plan p;
    int i, j;

    /* a QPSK-like constant-envelope sequence: |C40| ~ 1, amp_mean ~ 1, sigma_aa ~ 0 */
    for (i = 0; i < N; i++) {
        int s = (int)((urand() + 1.0) * 2.0) & 3;
        double a = 0.785398163397448 + 1.570796326794897 * s;
        x[2 * i] = (float)cos(a);
        x[2 * i + 1] = (float)sin(a);
    }
    for (i = 0; i < 2 * N; i++) {
        x[2 * N + i] = (float)urand();    /* noise-like */
        x[4 * N + i] = x[i] * 3.0f;       /* scaled copy of example 0 */
    }
    CHECK(knc_features_batch(x, 3, N, f) == KNC_OK);
    for (i = 0; i < 3 * KNC_NUM_FEATURES; i++)
        CHECK(isfinite(f[i]));
    CHECK(fabs(f[2] - 1.0) < 0.05);
    CHECK(f[4] < 0.05);
    CHECK(fabs(f[6] - 1.0) < 0.2);
    CHECK(fabs(f[10] - 1.0) < 0.2);
    /* scale invariance of everything except power_db and spectral flatness floor */
    CHECK(fabs(f[2 * KNC_NUM_FEATURES] - f[0] - 20 * log10(3.0)) < 1e-6);
    for (j = 1; j < KNC_NUM_FEATURES; j++)
        CHECK(fabs(f[2 * KNC_NUM_FEATURES + j] - f[j]) <= 1e-9 * (1 + fabs(f[j])));
    /* single == batch */
    CHECK(knc_fft_plan_init(&p, N) == KNC_OK);
    CHECK(knc_features(&p, x + 2 * N, N, work, g) == KNC_OK);
    for (j = 0; j < KNC_NUM_FEATURES; j++)
        CHECK(g[j] == f[KNC_NUM_FEATURES + j]);
    /* invalid: plan too small / too large, n < 2 */
    CHECK(knc_features(&p, x, N + 1, work, g) == KNC_EINVAL);
    CHECK(knc_features(&p, x, N / 2, work, g) == KNC_EINVAL);
    CHECK(knc_features_batch(x, 1, 1, g) == KNC_EINVAL);
    knc_fft_plan_free(&p);
    /* non power-of-two n uses zero padding */
    CHECK(knc_features_batch(x, 1, 200, g) == KNC_OK && isfinite(g[16]));
    CHECK(knc_feature_name(0)[0] == 'p' && knc_feature_name(99)[0] == '?');
    return TEST_EXIT();
}
