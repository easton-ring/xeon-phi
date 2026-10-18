/* Offload benchmark: batched magnitude-squared on the KNC coprocessor.
 *                                                    *** UNVERIFIED ***
 * Never compiled or run against a real card. Pragma and clause syntax follows
 * the Intel Language Extensions for Offload (LEO) as documented for Intel
 * C++ 13-17; sources and verification status are in docs/mpss-compatibility.md.
 * Requires the Intel compiler: there is no GCC equivalent of these pragmas.
 *
 * Build (MPSS guest, Intel compiler env loaded):  make -C mic/offload
 * Run:  build-mic/offload_magsq [--n-examples B] [--samples S] [--reps R]
 *                               [--chunks K] [--threads T]
 *   --threads T  OpenMP threads for the offloaded loop (num_threads clause on
 *                the card); 0 = the card runtime's default. The team size the
 *                loop actually got is measured on the card and recorded.
 *
 * Three patterns:
 *   resident  : transfer in once (alloc_if(1) free_if(0)), R compute-only offloads
 *               on resident buffers (alloc_if(0) free_if(0)), transfer out once.
 *               timing.kernel excludes transfers; timing.transfer = in + out.
 *   per_call  : R offloads that each copy in, compute, copy out, allocate/free.
 *   chunked   : the same B examples split into K offloads of B/K examples.
 *               For per_call and chunked, timing.kernel INCLUDES both transfers
 *               (end to end) and timing.transfer is null; offload.kernel_includes_transfer
 *               says which case a record is. Never compare the two kinds directly.
 *
 * Fail-loud rules (brief §1.3):
 *   - abort if _Offload_number_of_devices() == 0
 *   - every offload uses target(mic:0) with the `mandatory` clause (no host fallback)
 *   - the offloaded code reports whether __MIC__ was defined where it ran; if it
 *     ran on the host anyway, abort instead of reporting a result
 * Output: one JSON record per pattern (execution_mode "mic-offload").
 *
 * KNC_OFFLOAD_EMIT_TEST (tests only, any C99 compiler): builds just the
 * argument parser and the JSON emitter with a test main(); no offload code.
 */
#if !defined(__INTEL_OFFLOAD) && !defined(KNC_OFFLOAD_EMIT_TEST)
#error "offload_magsq.c must be built by the Intel compiler with offload enabled (icc, no -qno-offload)"
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifndef KNC_OFFLOAD_EMIT_TEST
#include <offload.h>
#include <omp.h>
#endif

#ifndef KNC_GIT_COMMIT
#define KNC_GIT_COMMIT "unknown"
#endif
#ifndef KNC_BUILD_FLAGS
#define KNC_BUILD_FLAGS "unknown"
#endif
#ifdef __INTEL_COMPILER
#define KNC_COMPILER_VERSION __INTEL_COMPILER
#else
#define KNC_COMPILER_VERSION 0
#endif
#ifndef _OPENMP
#define KNC_OPENMP_VERSION 0
#else
#define KNC_OPENMP_VERSION _OPENMP
#endif

/* Largest total buffer size accepted (input + output), below the card's 8 GB. */
#define MAX_BYTES (6.0 * 1024 * 1024 * 1024)

#ifndef KNC_OFFLOAD_EMIT_TEST
static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
#endif

/* KNC_SYNTAX_CHECK is only set by `make syntax-check` (gcc -fsyntax-only). */
#if defined(KNC_SYNTAX_CHECK) || defined(KNC_OFFLOAD_EMIT_TEST)
#define KNC_TARGET_MIC
#else
#define KNC_TARGET_MIC __attribute__((target(mic)))
#endif

#ifndef KNC_OFFLOAD_EMIT_TEST
/* Compiled for both host and coprocessor; returns 1 when executing on KNC.
 * *team receives the size of the team that ran the loop. */
KNC_TARGET_MIC static int magsq_kernel(const float *x, float *out, long n, int nt, int *team)
{
    long i;
    int t = 0;
    if (nt > 0) {
#pragma omp parallel num_threads(nt)
        {
#pragma omp single
            t = omp_get_num_threads();
#pragma omp for schedule(static)
            for (i = 0; i < n; i++)
                out[i] = x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1];
        }
    } else {
#pragma omp parallel
        {
#pragma omp single
            t = omp_get_num_threads();
#pragma omp for schedule(static)
            for (i = 0; i < n; i++)
                out[i] = x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1];
        }
    }
    *team = t;
#ifdef __MIC__
    return 1;
#else
    return 0;
#endif
}

static void require_mic(int on_mic, const char *where)
{
    if (!on_mic) {
        fprintf(stderr, "offload_magsq: %s executed on the HOST, not the coprocessor; refusing to report\n", where);
        exit(4);
    }
}
#endif /* !KNC_OFFLOAD_EMIT_TEST */

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Linear-interpolated percentile of sorted data (= numpy "linear", as knc_bench). */
static double pct(const double *s, long n, double q)
{
    double pos = q * (double)(n - 1);
    long i = (long)pos;
    double fr = pos - (double)i;
    return (i + 1 < n) ? s[i] + fr * (s[i + 1] - s[i]) : s[n - 1];
}

typedef struct {
    double max_abs, max_rel; /* max_rel only over elements whose reference is > 0 */
} errstat;

/* Compare every output element with a double-precision host reference. */
static errstat check_all(const float *x, const float *out, long n)
{
    errstat e = {0.0, 0.0};
    long i;
    for (i = 0; i < n; i++) {
        double r = (double)x[2 * i] * x[2 * i] + (double)x[2 * i + 1] * x[2 * i + 1];
        double d = fabs((double)out[i] - r);
        if (d > e.max_abs || d != d)
            e.max_abs = d;
        if (r > 0 && d / r > e.max_rel)
            e.max_rel = d / r;
    }
    return e;
}

static void jnum(double v)
{
    if (isfinite(v))
        printf("%.9g", v);
    else
        printf("null");
}

static int host_hypervisor_flag(void)
{
    char line[4096];
    int hyper = 0;
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp)
        return 0;
    while (fgets(line, sizeof line, fp))
        if (strncmp(line, "flags", 5) == 0 && strstr(line, " hypervisor"))
            hyper = 1;
    fclose(fp);
    return hyper;
}

typedef struct {
    long B, S, reps, chunks;
    int threads; /* requested on the card; 0 = runtime default */
} opts;

static void emit(const opts *o, const char *pattern, const double *kt_in, double t_in, double t_out,
                 errstat err, int mic_max_threads, int mic_team)
{
    double *kt = (double *)malloc((size_t)o->reps * sizeof(double));
    double med, q25, q75;
    char ts[32];
    struct utsname un;
    time_t tnow = time(NULL);
    long r;
    int transfer_inside = t_in < 0;

    if (!kt) {
        fprintf(stderr, "offload_magsq: out of memory\n");
        exit(1);
    }
    memcpy(kt, kt_in, (size_t)o->reps * sizeof(double));
    qsort(kt, (size_t)o->reps, sizeof(double), cmpd);
    med = pct(kt, o->reps, 0.5);
    q25 = pct(kt, o->reps, 0.25);
    q75 = pct(kt, o->reps, 0.75);
    strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", gmtime(&tnow));
    if (uname(&un) != 0)
        memset(&un, 0, sizeof un);

    printf("{\"schema_version\": \"1.0\", \"status\": \"ok\", \"simulated\": false, "
           "\"execution_mode\": \"mic-offload\", \"kernel\": \"magsq\", \"precision\": \"f32\", "
           "\"batch\": %ld, \"samples_per_example\": %ld, \"threads\": %d, \"warmup\": 1, \"reps\": %ld, ",
           o->B, o->S, mic_team, o->reps);
    printf("\"offload\": {\"pattern\": \"%s\", \"chunks\": %ld, \"device\": 0, "
           "\"kernel_includes_transfer\": %s, \"requested_threads\": %d, \"mic_team_size\": %d}, ",
           pattern, o->chunks, transfer_inside ? "true" : "false", o->threads, mic_team);
    printf("\"input\": {\"kind\": \"synthetic\", \"path\": null, \"sha256\": null, \"wrapped\": false}, "
           "\"timing\": {\"unit\": \"s\", \"init\": 0, \"load\": 0, \"transfer\": ");
    if (transfer_inside)
        printf("null");
    else
        jnum(t_in + t_out);
    printf(", \"kernel\": {\"median\": %.9g, \"min\": %.9g, \"max\": %.9g, \"q25\": %.9g, \"q75\": %.9g, "
           "\"iqr\": %.9g, \"samples\": [",
           med, kt[0], kt[o->reps - 1], q25, q75, q75 - q25);
    for (r = 0; r < o->reps; r++)          /* raw samples in measurement order */
        printf("%s%.9g", r ? ", " : "", kt_in[r]);
    printf("]}, \"post\": 0}, \"throughput\": {\"complex_samples_per_s\": %.9g, \"examples_per_s\": %.9g}, ",
           (double)(o->B * o->S) / med, (double)o->B / med);
    printf("\"error\": {\"reference\": \"naive double-precision C loop (host), every element\", "
           "\"examples_checked\": %ld, \"metric\": \"max_abs = max |out-ref|; max_rel = max |out-ref|/ref "
           "over elements with ref > 0\", \"max_abs\": ", o->B);
    jnum(err.max_abs);
    printf(", \"max_rel\": ");
    jnum(err.max_rel);
    printf("}, \"metadata\": {\"timestamp_utc\": \"%s\", "
           "\"cpu_model\": \"Intel Xeon Phi (offload target mic:0); host side: see os\", "
           "\"cpu_hypervisor_flag\": %s, \"online_cpus\": %ld, "
           "\"os\": {\"sysname\": \"%s\", \"release\": \"%s\", \"machine\": \"%s\"}, "
           "\"compiler\": \"icc %d\", \"build_flags\": \"%s\", "
           "\"openmp\": {\"version\": %d, \"max_threads\": %d, \"team_size\": %d, \"env\": {}}, "
           "\"toolchain\": {\"name\": \"icc-offload\", \"vector_isa\": \"IMCI via icc auto-vectorization (not verified)\"}, "
           "\"git_commit\": \"%s\"}}\n",
           ts, host_hypervisor_flag() ? "true" : "false", sysconf(_SC_NPROCESSORS_ONLN),
           un.sysname, un.release, un.machine, KNC_COMPILER_VERSION, KNC_BUILD_FLAGS,
           KNC_OPENMP_VERSION, mic_max_threads > 0 ? mic_max_threads : 1, mic_team > 0 ? mic_team : 1,
           KNC_GIT_COMMIT);
    free(kt);
}

/* Strict integer option: whole string, base 10, in [lo, hi]. */
static long parse_long(const char *opt, const char *s, long lo, long hi)
{
    char *end;
    long v;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "offload_magsq: %s needs an integer in [%ld, %ld], got '%s'\n", opt, lo, hi, s);
        exit(2);
    }
    return v;
}

static void parse_args(int argc, char **argv, opts *o)
{
    int i;
    o->B = 1024; o->S = 1024; o->reps = 20; o->chunks = 16; o->threads = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (i + 1 >= argc) {
            fprintf(stderr, "offload_magsq: option %s is missing its value\n", a);
            exit(2);
        }
        if (!strcmp(a, "--n-examples")) o->B = parse_long(a, argv[++i], 1, LONG_MAX);
        else if (!strcmp(a, "--samples")) o->S = parse_long(a, argv[++i], 1, LONG_MAX);
        else if (!strcmp(a, "--reps")) o->reps = parse_long(a, argv[++i], 1, 1000000);
        else if (!strcmp(a, "--chunks")) o->chunks = parse_long(a, argv[++i], 1, LONG_MAX);
        else if (!strcmp(a, "--threads")) o->threads = (int)parse_long(a, argv[++i], 0, 1024);
        else {
            fprintf(stderr, "offload_magsq: unknown option %s\n", a);
            exit(2);
        }
    }
    if (o->B % o->chunks != 0) {
        fprintf(stderr, "offload_magsq: --n-examples (%ld) must be divisible by --chunks (%ld)\n", o->B, o->chunks);
        exit(2);
    }
    /* n = B*S complex samples: 8 bytes in + 4 bytes out each */
    if (o->B > LONG_MAX / o->S || (double)o->B * (double)o->S * 12.0 > MAX_BYTES ||
        (size_t)(o->B * o->S) > (size_t)-1 / (2 * sizeof(float))) {
        fprintf(stderr, "offload_magsq: --n-examples x --samples too large (limit %.0f bytes)\n", MAX_BYTES);
        exit(2);
    }
}

#ifdef KNC_OFFLOAD_EMIT_TEST
/* Test main: parse arguments, then emit one record per pattern from synthetic
 * timings and a host-computed output. Exercises parsing and JSON only. */
int main(int argc, char **argv)
{
    opts o;
    long i, n;
    float *x, *out;
    double *kt;
    parse_args(argc, argv, &o);
    n = o.B * o.S;
    x = (float *)malloc((size_t)(2 * n) * sizeof(float));
    out = (float *)malloc((size_t)n * sizeof(float));
    kt = (double *)malloc((size_t)o.reps * sizeof(double));
    if (!x || !out || !kt)
        return 1;
    for (i = 0; i < 2 * n; i++)
        x[i] = (float)sin(0.001 * (double)i);
    for (i = 0; i < n; i++)
        out[i] = x[2 * i] * x[2 * i] + x[2 * i + 1] * x[2 * i + 1];
    for (i = 0; i < o.reps; i++)
        kt[i] = 1e-3 * (double)(o.reps - i);
    emit(&o, "resident", kt, 2e-4, 1e-4, check_all(x, out, n), 236, o.threads ? o.threads : 236);
    emit(&o, "per_call", kt, -1.0, 0.0, check_all(x, out, n), 236, o.threads ? o.threads : 236);
    emit(&o, "chunked", kt, -1.0, 0.0, check_all(x, out, n), 236, o.threads ? o.threads : 236);
    free(x);
    free(out);
    free(kt);
    return 0;
}
#else
int main(int argc, char **argv)
{
    opts o;
    long n, r, c, i;
    int ndev, mic_max = 0, team = 0, nt, on_mic = 0;
    float *x, *out;
    double *kt, t0, t_in, t_out;

    parse_args(argc, argv, &o);
    nt = o.threads;
    ndev = _Offload_number_of_devices();
    if (ndev < 1) {
        fprintf(stderr, "offload_magsq: _Offload_number_of_devices() = %d; no coprocessor, aborting\n", ndev);
        return 3;
    }
    n = o.B * o.S;
    if (posix_memalign((void **)&x, 64, (size_t)(2 * n) * sizeof(float)) != 0 ||
        posix_memalign((void **)&out, 64, (size_t)n * sizeof(float)) != 0 ||
        !(kt = (double *)malloc((size_t)o.reps * sizeof(double))))
        return 1;
    for (i = 0; i < 2 * n; i++)
        x[i] = (float)sin(0.001 * (double)i);

#pragma offload target(mic:0) mandatory out(mic_max)
    {
        mic_max = omp_get_max_threads();
    }

    /* ---- resident: one transfer in, compute-only offloads, one transfer out */
    t0 = now();
#pragma offload_transfer target(mic:0) mandatory in(x : length(2 * n) alloc_if(1) free_if(0)) \
    nocopy(out : length(n) alloc_if(1) free_if(0))
    t_in = now() - t0;
    for (r = -1; r < o.reps; r++) { /* r = -1 is the warm-up */
        double ts = now();
#pragma offload target(mic:0) mandatory nocopy(x : length(2 * n) alloc_if(0) free_if(0)) \
    nocopy(out : length(n) alloc_if(0) free_if(0)) in(nt) out(on_mic, team)
        on_mic = magsq_kernel(x, out, n, nt, &team);
        if (r >= 0)
            kt[r] = now() - ts;
        require_mic(on_mic, "resident offload");
    }
    t0 = now();
#pragma offload_transfer target(mic:0) mandatory out(out : length(n) alloc_if(0) free_if(0))
    t_out = now() - t0;
#pragma offload_transfer target(mic:0) mandatory nocopy(x : length(2 * n) alloc_if(0) free_if(1)) \
    nocopy(out : length(n) alloc_if(0) free_if(1))
    emit(&o, "resident", kt, t_in, t_out, check_all(x, out, n), mic_max, team);

    /* ---- per_call: every offload copies in, computes, copies out (allocate + free) */
    memset(out, 0, (size_t)n * sizeof(float));
    for (r = -1; r < o.reps; r++) {
        double ts = now();
#pragma offload target(mic:0) mandatory in(x : length(2 * n) alloc_if(1) free_if(1)) \
    out(out : length(n) alloc_if(1) free_if(1)) in(nt) out(on_mic, team)
        on_mic = magsq_kernel(x, out, n, nt, &team);
        if (r >= 0)
            kt[r] = now() - ts; /* includes both transfers */
        require_mic(on_mic, "per-call offload");
    }
    emit(&o, "per_call", kt, -1.0, 0.0, check_all(x, out, n), mic_max, team);

    /* ---- chunked: K offloads of B/K examples, each with its own transfers */
    memset(out, 0, (size_t)n * sizeof(float));
    for (r = -1; r < o.reps; r++) {
        double ts = now();
        long m = n / o.chunks;
        for (c = 0; c < o.chunks; c++) {
            float *xc = x + 2 * c * m, *oc = out + c * m;
#pragma offload target(mic:0) mandatory in(xc : length(2 * m) alloc_if(1) free_if(1)) \
    out(oc : length(m) alloc_if(1) free_if(1)) in(nt) out(on_mic, team)
            on_mic = magsq_kernel(xc, oc, m, nt, &team);
            require_mic(on_mic, "chunked offload");
        }
        if (r >= 0)
            kt[r] = now() - ts;
    }
    emit(&o, "chunked", kt, -1.0, 0.0, check_all(x, out, n), mic_max, team);

    free(x);
    free(out);
    free(kt);
    return 0;
}
#endif
