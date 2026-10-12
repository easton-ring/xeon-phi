/* Offload benchmark: batched magnitude-squared on the KNC coprocessor.
 *                                                    *** UNVERIFIED ***
 * Never compiled or run against a real card. Pragma and clause syntax follows
 * the Intel Language Extensions for Offload (LEO) as documented for Intel
 * C++ 13-17; sources and verification status are in docs/mpss-compatibility.md.
 *
 * Build (MPSS guest, Intel compiler env loaded):  make -C mic/offload
 * Run:  build-mic/offload_magsq [--n-examples B] [--samples S] [--reps R] [--chunks K]
 *
 * Three patterns, timed separately (transfer vs compute):
 *   resident  : transfer in once (alloc_if(1) free_if(0)), R compute-only offloads
 *               on resident buffers (alloc_if(0) free_if(0)), transfer out once
 *   per_call  : R offloads that each copy in, compute, copy out, and allocate/free
 *   chunked   : the same B examples split into K offloads of B/K examples
 *               (many small batches vs one large batch)
 *
 * Fail-loud rules (brief §1.3):
 *   - abort if _Offload_number_of_devices() == 0
 *   - every offload uses target(mic:0) with the `mandatory` clause (no host fallback)
 *   - the offloaded code reports whether __MIC__ was defined where it ran; if it
 *     ran on the host anyway, abort instead of reporting a result
 * Output: one JSON record per pattern (execution_mode "mic-offload").
 */
#ifndef __INTEL_OFFLOAD
#error "offload_magsq.c must be built by the Intel compiler with offload enabled (icc, no -qno-offload)"
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <offload.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef KNC_GIT_COMMIT
#define KNC_GIT_COMMIT "unknown"
#endif
#ifndef KNC_BUILD_FLAGS
#define KNC_BUILD_FLAGS "unknown"
#endif

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* KNC_SYNTAX_CHECK is only set by `make syntax-check` (gcc -fsyntax-only). */
#ifdef KNC_SYNTAX_CHECK
#define KNC_TARGET_MIC
#else
#define KNC_TARGET_MIC __attribute__((target(mic)))
#endif

/* Compiled for both host and coprocessor; returns 1 when executing on KNC. */
KNC_TARGET_MIC static int magsq_kernel(const float *x, float *out, long n)
{
    long i;
#pragma omp parallel for schedule(static)
    for (i = 0; i < n; i++)
        out[i] = x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1];
#ifdef __MIC__
    return 1;
#else
    return 0;
#endif
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void require_mic(int on_mic, const char *where)
{
    if (!on_mic) {
        fprintf(stderr, "offload_magsq: %s executed on the HOST, not the coprocessor; refusing to report\n", where);
        exit(4);
    }
}

static double max_rel_err(const float *x, const float *out, long n)
{
    double m = 0;
    long i;
    for (i = 0; i < n; i++) {
        double r = (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        double e = fabs(out[i] - r) / (r > 0 ? r : 1);
        if (e > m)
            m = e;
    }
    return m;
}

static void emit(const char *pattern, long B, long S, long reps, long chunks, int threads,
                 double *kt, double t_in, double t_out, double err)
{
    double med, q25, q75;
    char tbuf[64];
    long r;
    /* transfer is separate only for the resident pattern; otherwise it is inside "kernel" */
    if (t_in < 0)
        snprintf(tbuf, sizeof tbuf, "null");
    else
        snprintf(tbuf, sizeof tbuf, "%.9g", t_in + t_out);
    qsort(kt, (size_t)reps, sizeof(double), cmpd);
    med = kt[reps / 2];
    q25 = kt[reps / 4];
    q75 = kt[(3 * reps) / 4];
    printf("{\"schema_version\": \"1.0\", \"status\": \"ok\", \"simulated\": false, "
           "\"execution_mode\": \"mic-offload\", \"kernel\": \"magsq\", \"precision\": \"f32\", "
           "\"batch\": %ld, \"samples_per_example\": %ld, \"threads\": %d, \"warmup\": 1, \"reps\": %ld, "
           "\"offload\": {\"pattern\": \"%s\", \"chunks\": %ld, \"device\": 0}, "
           "\"input\": {\"kind\": \"synthetic\", \"path\": null, \"sha256\": null, \"wrapped\": false}, "
           "\"timing\": {\"unit\": \"s\", \"init\": 0, \"load\": 0, \"transfer\": %s, "
           "\"kernel\": {\"median\": %.9g, \"min\": %.9g, \"max\": %.9g, \"q25\": %.9g, \"q75\": %.9g, "
           "\"iqr\": %.9g, \"samples\": [",
           B, S, threads, reps, pattern, chunks, tbuf, med, kt[0], kt[reps - 1], q25, q75, q75 - q25);
    for (r = 0; r < reps; r++)
        printf("%s%.9g", r ? ", " : "", kt[r]);
    printf("]}, \"post\": 0}, \"throughput\": {\"complex_samples_per_s\": %.9g, \"examples_per_s\": %.9g}, "
           "\"error\": {\"reference\": \"naive double-precision C loop (host)\", \"examples_checked\": %ld, "
           "\"metric\": \"elementwise |out-ref|/|ref|\", \"max_abs\": null, \"max_rel\": %.9g}, "
           "\"metadata\": {\"timestamp_utc\": \"\", \"cpu_model\": \"Intel Xeon Phi (offload target mic:0)\", "
           "\"cpu_hypervisor_flag\": false, \"online_cpus\": %d, \"os\": {\"sysname\": \"\", \"release\": \"\", "
           "\"machine\": \"k1om\"}, \"compiler\": \"icc %d\", \"build_flags\": \"%s\", "
           "\"openmp\": {\"version\": %d, \"max_threads\": %d, \"env\": {}}, \"git_commit\": \"%s\"}}\n",
           (double)(B * S) / med, (double)B / med, B, err, threads, __INTEL_COMPILER, KNC_BUILD_FLAGS,
           _OPENMP, threads, KNC_GIT_COMMIT);
}

int main(int argc, char **argv)
{
    long B = 1024, S = 1024, reps = 20, chunks = 16, n, r, c, i;
    int ndev, threads = 0, on_mic = 0;
    float *x, *out;
    double *kt, t0, t_in, t_out;

    for (i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--n-examples")) B = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--samples")) S = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--reps")) reps = atol(argv[i + 1]);
        else if (!strcmp(argv[i], "--chunks")) chunks = atol(argv[i + 1]);
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    if (B < 1 || S < 1 || reps < 1 || chunks < 1 || B % chunks != 0) {
        fprintf(stderr, "offload_magsq: need positive sizes and B divisible by --chunks\n");
        return 2;
    }
    ndev = _Offload_number_of_devices();
    if (ndev < 1) {
        fprintf(stderr, "offload_magsq: _Offload_number_of_devices() = %d; no coprocessor, aborting\n", ndev);
        return 3;
    }
    n = B * S;
    if (posix_memalign((void **)&x, 64, (size_t)(2 * n) * sizeof(float)) != 0 ||
        posix_memalign((void **)&out, 64, (size_t)n * sizeof(float)) != 0 ||
        !(kt = (double *)malloc((size_t)reps * sizeof(double))))
        return 1;
    for (i = 0; i < 2 * n; i++)
        x[i] = (float)sin(0.001 * (double)i);

#pragma offload target(mic:0) mandatory out(threads)
    {
        threads = omp_get_max_threads();
    }

    /* ---- resident: one transfer in, compute-only offloads, one transfer out */
    t0 = now();
#pragma offload_transfer target(mic:0) mandatory in(x : length(2 * n) alloc_if(1) free_if(0)) \
    nocopy(out : length(n) alloc_if(1) free_if(0))
    t_in = now() - t0;
    for (r = -1; r < reps; r++) { /* r = -1 is the warm-up */
        double ts = now();
#pragma offload target(mic:0) mandatory nocopy(x : length(2 * n) alloc_if(0) free_if(0)) \
    nocopy(out : length(n) alloc_if(0) free_if(0)) out(on_mic)
        on_mic = magsq_kernel(x, out, n);
        if (r >= 0)
            kt[r] = now() - ts;
        require_mic(on_mic, "resident offload");
    }
    t0 = now();
#pragma offload_transfer target(mic:0) mandatory out(out : length(n) alloc_if(0) free_if(0))
    t_out = now() - t0;
#pragma offload_transfer target(mic:0) mandatory nocopy(x : length(2 * n) alloc_if(0) free_if(1)) \
    nocopy(out : length(n) alloc_if(0) free_if(1))
    emit("resident", B, S, reps, 1, threads, kt, t_in, t_out, max_rel_err(x, out, n));

    /* ---- per_call: every offload copies in, computes, copies out (allocate + free) */
    memset(out, 0, (size_t)n * sizeof(float));
    for (r = -1; r < reps; r++) {
        double ts = now();
#pragma offload target(mic:0) mandatory in(x : length(2 * n) alloc_if(1) free_if(1)) \
    out(out : length(n) alloc_if(1) free_if(1)) out(on_mic)
        on_mic = magsq_kernel(x, out, n);
        if (r >= 0)
            kt[r] = now() - ts; /* includes both transfers */
        require_mic(on_mic, "per-call offload");
    }
    emit("per_call", B, S, reps, 1, threads, kt, -1.0, 0.0, max_rel_err(x, out, n));

    /* ---- chunked: K offloads of B/K examples, each with its own transfers */
    memset(out, 0, (size_t)n * sizeof(float));
    for (r = -1; r < reps; r++) {
        double ts = now();
        long m = n / chunks;
        for (c = 0; c < chunks; c++) {
            float *xc = x + 2 * c * m, *oc = out + c * m;
#pragma offload target(mic:0) mandatory in(xc : length(2 * m) alloc_if(1) free_if(1)) \
    out(oc : length(m) alloc_if(1) free_if(1)) out(on_mic)
            on_mic = magsq_kernel(xc, oc, m);
            require_mic(on_mic, "chunked offload");
        }
        if (r >= 0)
            kt[r] = now() - ts;
    }
    emit("chunked", B, S, reps, chunks, threads, kt, -1.0, 0.0, max_rel_err(x, out, n));

    free(x);
    free(out);
    free(kt);
    return 0;
}
