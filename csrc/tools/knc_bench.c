/* knc_bench: time one kernel configuration and print one JSON record.
 *
 *   knc_bench --kernel K (--dataset FILE | --synthetic) [--samples S]
 *             [--batch B] [--threads T] [--warmup W] [--reps R]
 *             [--precision f32|f64] [--xcorr-len L] [--out FILE]
 *
 * Kernels: cmul magsq meanpow psd fft xcorr cfo features.
 * Record schema: python/knc_rf_lab/schemas/benchmark.schema.json and
 * docs/benchmark-methodology.md. The binary records its own metadata
 * (compiler, flags, commit, CPU, dataset SHA-256), so a run on the
 * coprocessor needs no Python. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "knc/features.h"
#include "knc/fft.h"
#include "knc/kernels.h"
#include "knc/kncds.h"
#include "knc/sha256.h"
#include "knc/timing.h"

#ifndef KNC_GIT_COMMIT
#define KNC_GIT_COMMIT "unknown"
#endif
#ifndef KNC_BUILD_FLAGS
#define KNC_BUILD_FLAGS "unknown"
#endif
#ifndef KNC_EXEC_MODE
#define KNC_EXEC_MODE "host"
#endif

/* A binary labeled mic-native must be a KNC (-mmic) build and must run on the card. */
#if defined(KNC_REQUIRE_MIC) && !defined(__MIC__)
#error "KNC_REQUIRE_MIC set but __MIC__ is not defined: this is not a -mmic build"
#endif

#define TWO_PI 6.283185307179586476925286766559
#define NREF 4 /* examples checked against the double reference */

typedef struct {
    const char *kernel, *dataset, *out, *precision;
    long samples, batch, threads, warmup, reps, xlen;
    int synthetic;
} opts;

static void die(const char *msg)
{
    fprintf(stderr, "knc_bench: %s\n", msg);
    exit(2);
}

static void *amalloc(size_t bytes)
{
    void *p = NULL;
    if (bytes == 0)
        bytes = 64;
    if (posix_memalign(&p, 64, bytes) != 0)
        die("out of memory");
    return p;
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Linear-interpolated percentile of sorted data (= numpy "linear"). */
static double pct(const double *s, long n, double q)
{
    double pos = q * (double)(n - 1);
    long i = (long)pos;
    double fr = pos - (double)i;
    return (i + 1 < n) ? s[i] + fr * (s[i + 1] - s[i]) : s[n - 1];
}

static void jnum(FILE *o, double v)
{
    if (isfinite(v))
        fprintf(o, "%.9g", v);
    else
        fprintf(o, "null");
}

static void jstr(FILE *o, const char *s)
{
    fputc('"', o);
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            fputc('\\', o);
        if ((unsigned char)*s >= 0x20)
            fputc(*s, o);
    }
    fputc('"', o);
}

static void cpu_info(char *model, size_t len, int *hypervisor)
{
    char line[4096];
    FILE *fp = fopen("/proc/cpuinfo", "r");
    snprintf(model, len, "unknown");
    *hypervisor = 0;
    if (!fp)
        return;
    while (fgets(line, sizeof line, fp)) {
        char *c = strchr(line, ':');
        if (!c)
            continue;
        if (strncmp(line, "model name", 10) == 0 && strcmp(model, "unknown") == 0) {
            c++;
            while (*c == ' ' || *c == '\t')
                c++;
            snprintf(model, len, "%s", c);
            model[strcspn(model, "\n")] = '\0';
        }
        if (strncmp(line, "flags", 5) == 0 && strstr(c, " hypervisor"))
            *hypervisor = 1;
    }
    fclose(fp);
}

static void synth(float *x, long batch, long n)
{
    unsigned long s = 2463534242u;
    long b, i;
    for (b = 0; b < batch; b++)
        for (i = 0; i < n; i++) {
            double ph = TWO_PI * (0.01 * (double)(b % 7 + 1)) * (double)i;
            double nr, ni;
            s = (s * 1103515245u + 12345u) & 0x7fffffffu;
            nr = (double)s / 2147483648.0 - 0.5;
            s = (s * 1103515245u + 12345u) & 0x7fffffffu;
            ni = (double)s / 2147483648.0 - 0.5;
            x[2 * (b * n + i)] = (float)(cos(ph) + 0.3 * nr);
            x[2 * (b * n + i) + 1] = (float)(sin(ph) + 0.3 * ni);
        }
}

/* Naive double DFT of one example (reference for fft/psd). */
static void dft(const double *x, long n, double *X)
{
    long k, j;
    for (k = 0; k < n; k++) {
        double re = 0, im = 0;
        for (j = 0; j < n; j++) {
            double a = -TWO_PI * (double)((k * j) % n) / (double)n;
            re += x[2 * j] * cos(a) - x[2 * j + 1] * sin(a);
            im += x[2 * j] * sin(a) + x[2 * j + 1] * cos(a);
        }
        X[2 * k] = re;
        X[2 * k + 1] = im;
    }
}

typedef struct {
    double max_abs, max_rel;
    const char *metric;
} errstat;

/* value of output element i as double, whichever precision */
#define OUTV(i) (f64 ? ((double *)out)[i] : (double)((float *)out)[i])

static errstat check(const char *k, int f64, const float *x, const float *y, const void *out,
                     long batch, long n, long xlen)
{
    errstat e = {0, 0, "elementwise |out-ref|/|ref|"};
    long nb = batch < NREF ? batch : NREF, b, i, m;
    double *X = (double *)malloc((size_t)(2 * n) * sizeof(double));
    double *W = (double *)malloc((size_t)(2 * n) * sizeof(double));
    for (b = 0; b < nb; b++) {
        const float *xb = x + 2 * b * n;
        if (!strcmp(k, "cmul") || !strcmp(k, "magsq")) {
            for (i = 0; i < n; i++) {
                double a = xb[2 * i], bb = xb[2 * i + 1], d, r;
                if (!strcmp(k, "magsq")) {
                    r = a * a + bb * bb;
                    d = fabs(OUTV(b * n + i) - r);
                } else {
                    double c = y[2 * (b * n + i)], dd = y[2 * (b * n + i) + 1];
                    double rr = a * c - bb * dd, ri = a * dd + bb * c;
                    double er = OUTV(2 * (b * n + i)) - rr, ei = OUTV(2 * (b * n + i) + 1) - ri;
                    d = sqrt(er * er + ei * ei);
                    r = sqrt(rr * rr + ri * ri);
                }
                if (d > e.max_abs) e.max_abs = d;
                if (r > 0 && d / r > e.max_rel) e.max_rel = d / r;
            }
        } else if (!strcmp(k, "meanpow") || !strcmp(k, "cfo")) {
            double re = 0, im = 0, r, d;
            e.metric = !strcmp(k, "cfo") ? "per example |f-ref| (cycles/sample)" : "per example |out-ref|/|ref|";
            for (i = 0; i + 1 < n || (i < n && k[0] == 'm'); i++) {
                if (k[0] == 'm') {
                    re += (double)xb[2 * i] * xb[2 * i] + (double)xb[2 * i + 1] * xb[2 * i + 1];
                } else {
                    double a = xb[2 * i + 2], bb = xb[2 * i + 3], c = xb[2 * i], dd = xb[2 * i + 1];
                    re += a * c + bb * dd;
                    im += bb * c - a * dd;
                }
            }
            r = k[0] == 'm' ? re / (double)n : atan2(im, re) / TWO_PI;
            d = fabs(OUTV(b) - r);
            if (d > e.max_abs) e.max_abs = d;
            if (fabs(r) > 0 && d / fabs(r) > e.max_rel) e.max_rel = d / fabs(r);
        } else if (!strcmp(k, "fft") || !strcmp(k, "psd") || !strcmp(k, "xcorr")) {
            double num = 0, den = 0, mx = 0;
            e.metric = "per example ||out-ref||_2/||ref||_2 (normwise)";
            if (!strcmp(k, "xcorr")) {
                long nout = n - xlen + 1;
                for (i = 0; i < nout; i++) {
                    double re = 0, im = 0, er, ei;
                    for (m = 0; m < xlen; m++) {
                        double a = xb[2 * (i + m)], bb = xb[2 * (i + m) + 1], c = x[2 * m], dd = x[2 * m + 1];
                        re += a * c + bb * dd;
                        im += bb * c - a * dd;
                    }
                    er = OUTV(2 * (b * nout + i)) - re;
                    ei = OUTV(2 * (b * nout + i) + 1) - im;
                    num += er * er + ei * ei;
                    den += re * re + im * im;
                    if (sqrt(er * er + ei * ei) > mx) mx = sqrt(er * er + ei * ei);
                }
            } else if (!strcmp(k, "fft")) {
                for (i = 0; i < 2 * n; i++)
                    W[i] = xb[i];
                dft(W, n, X);
                for (i = 0; i < 2 * n; i++) {
                    double d = OUTV(2 * b * n + i) - X[i];
                    num += d * d;
                    den += X[i] * X[i];
                    if (fabs(d) > mx) mx = fabs(d);
                }
            } else {
                double ss = 0;
                for (i = 0; i < n; i++) {
                    double hw = 0.5 - 0.5 * cos(TWO_PI * (double)i / (double)n);
                    ss += hw * hw;
                    W[2 * i] = xb[2 * i] * hw;
                    W[2 * i + 1] = xb[2 * i + 1] * hw;
                }
                dft(W, n, X);
                for (i = 0; i < n; i++) {
                    double r = (X[2 * i] * X[2 * i] + X[2 * i + 1] * X[2 * i + 1]) / ss;
                    double d = OUTV(b * n + i) - r;
                    num += d * d;
                    den += r * r;
                    if (fabs(d) > mx) mx = fabs(d);
                }
            }
            if (mx > e.max_abs) e.max_abs = mx;
            if (den > 0 && sqrt(num / den) > e.max_rel) e.max_rel = sqrt(num / den);
        } else {
            e.metric = NULL; /* features: checked against NumPy in tests/test_c_core.py */
        }
    }
    free(X);
    free(W);
    return e;
}

static long argl(char **argv, int *i, int argc)
{
    char *end;
    long v;
    if (*i + 1 >= argc)
        die("missing value");
    v = strtol(argv[++*i], &end, 10);
    if (*end || v < 0)
        die("bad integer argument");
    return v;
}

int main(int argc, char **argv)
{
    opts o = {NULL, NULL, NULL, "f32", 1024, 64, 1, 3, 20, 64, 0};
    kncds_file ds;
    knc_fft_plan plan;
    char sha[65] = "", cpu[256], tbuf[32];
    struct utsname un;
    int i, f64, hyper, have_plan = 0, wrapped = 0, rc = 0;
    long n, B, r, outn;
    double t0, t_init, t_load, t_post, *times, *sorted;
    float *x32, *y32 = NULL;
    void *x = NULL, *y = NULL, *out = NULL, *work = NULL;
    size_t esz;
    errstat err;
    time_t now = time(NULL);
    FILE *fo = stdout;

#ifdef KNC_REQUIRE_MIC
    /* Fail loudly unless we are on the coprocessor (uname -m on the card is
     * expected to be "k1om"; UNVERIFIED, see docs/mpss-compatibility.md). */
    if (uname(&un) != 0 || strcmp(un.machine, "k1om") != 0) {
        fprintf(stderr, "knc_bench: mic-native build is not running on a KNC coprocessor (uname -m = %s)\n",
                un.machine);
        return 3;
    }
#endif
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--kernel") && i + 1 < argc) o.kernel = argv[++i];
        else if (!strcmp(argv[i], "--dataset") && i + 1 < argc) o.dataset = argv[++i];
        else if (!strcmp(argv[i], "--synthetic")) o.synthetic = 1;
        else if (!strcmp(argv[i], "--samples")) o.samples = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--batch")) o.batch = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--threads")) o.threads = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--warmup")) o.warmup = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--reps")) o.reps = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--xcorr-len")) o.xlen = argl(argv, &i, argc);
        else if (!strcmp(argv[i], "--precision") && i + 1 < argc) o.precision = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) o.out = argv[++i];
        else {
            fprintf(stderr, "usage: %s --kernel cmul|magsq|meanpow|psd|fft|xcorr|cfo|features "
                            "(--dataset F | --synthetic [--samples S]) [--batch B] [--threads T] "
                            "[--warmup W] [--reps R] [--precision f32|f64] [--xcorr-len L] [--out F]\n", argv[0]);
            return 2;
        }
    }
    if (!o.kernel || (!o.dataset == !o.synthetic))
        die("need --kernel and exactly one of --dataset/--synthetic");
    if (strcmp(o.precision, "f32") && strcmp(o.precision, "f64"))
        die("--precision must be f32 or f64");
    f64 = !strcmp(o.precision, "f64");
    if (!strcmp(o.kernel, "features") && f64)
        die("features read float32 input and compute in double; use --precision f32");
    if (o.batch < 1 || o.reps < 1 || o.threads < 1)
        die("batch, reps and threads must be >= 1");
#ifdef _OPENMP
    omp_set_num_threads((int)o.threads);
#else
    if (o.threads != 1)
        die("built without OpenMP: only --threads 1 is valid");
#endif

    /* ---- init: buffers and plans */
    t0 = knc_now();
    if (o.dataset) {
        int st = kncds_open(&ds, o.dataset);
        if (st != KNCDS_OK) {
            fprintf(stderr, "knc_bench: %s: %s %s\n", o.dataset, kncds_strerror(st), ds.msg);
            return 1;
        }
        if (ds.h.num_examples == 0)
            die("dataset is empty");
        o.samples = (long)ds.h.samples_per_example;
    }
    n = o.samples;
    B = o.batch;
    if (n < 2)
        die("need >= 2 samples per example");
    if (!strcmp(o.kernel, "xcorr") && (o.xlen < 1 || o.xlen > n))
        die("--xcorr-len must be in [1, samples]");
    esz = f64 ? sizeof(double) : sizeof(float);
    x32 = (float *)amalloc((size_t)(2 * B * n) * sizeof(float));
    outn = 2 * B * n;
    if (!strcmp(o.kernel, "features"))
        outn = B * KNC_NUM_FEATURES;
    x = f64 ? amalloc((size_t)(2 * B * n) * esz) : (void *)x32;
    out = amalloc((size_t)outn * (f64 || !strcmp(o.kernel, "features") ? sizeof(double) : sizeof(float)));
    /* features output is double regardless of --precision */
    if (!strcmp(o.kernel, "psd") || !strcmp(o.kernel, "fft")) {
        if (knc_fft_plan_init(&plan, (size_t)n) != KNC_OK)
            die("fft/psd need a power-of-two sample count");
        have_plan = 1;
        work = amalloc((size_t)(2 * B * n) * esz);
    }
    if (!strcmp(o.kernel, "cmul")) {
        long j;
        y32 = (float *)amalloc((size_t)(2 * B * n) * sizeof(float));
        for (j = 0; j < B * n; j++) {
            double ph = TWO_PI * 0.01 * (double)(j % n);
            y32[2 * j] = (float)cos(ph);
            y32[2 * j + 1] = (float)sin(ph);
        }
        y = y32;
        if (f64) {
            y = amalloc((size_t)(2 * B * n) * sizeof(double));
            for (j = 0; j < 2 * B * n; j++)
                ((double *)y)[j] = y32[j];
        }
    }
    times = (double *)malloc((size_t)o.reps * sizeof(double));
    sorted = (double *)malloc((size_t)o.reps * sizeof(double));
    t_init = knc_now() - t0;

    /* ---- load */
    t0 = knc_now();
    if (o.dataset) {
        long got = 0;
        while (got < B) {
            long take = B - got;
            if ((uint64_t)take > ds.h.num_examples)
                take = (long)ds.h.num_examples;
            if (kncds_read(&ds, 0, (uint64_t)take, x32 + 2 * got * n) != KNCDS_OK)
                die("dataset read failed");
            got += take;
            if (got < B)
                wrapped = 1;
        }
    } else {
        synth(x32, B, n);
    }
    if (f64) {
        long j;
        for (j = 0; j < 2 * B * n; j++)
            ((double *)x)[j] = x32[j];
    }
    t_load = knc_now() - t0;

    /* ---- kernel */
    for (r = -o.warmup; r < o.reps; r++) {
        double ts;
        int st = 0;
        if (!strcmp(o.kernel, "fft"))
            memcpy(work, x, (size_t)(2 * B * n) * esz); /* in-place transform: fresh input each rep */
        ts = knc_now();
        if (!strcmp(o.kernel, "cmul"))
            st = f64 ? knc_cmul_f64((double *)x, (double *)y, (double *)out, (size_t)(B * n))
                     : knc_cmul_f32(x32, y32, (float *)out, (size_t)(B * n));
        else if (!strcmp(o.kernel, "magsq"))
            st = f64 ? knc_magsq_f64((double *)x, (double *)out, (size_t)(B * n))
                     : knc_magsq_f32(x32, (float *)out, (size_t)(B * n));
        else if (!strcmp(o.kernel, "meanpow"))
            st = f64 ? knc_mean_power_f64((double *)x, (size_t)B, (size_t)n, (double *)out)
                     : knc_mean_power_f32(x32, (size_t)B, (size_t)n, (float *)out);
        else if (!strcmp(o.kernel, "cfo"))
            st = f64 ? knc_cfo_f64((double *)x, (size_t)B, (size_t)n, (double *)out)
                     : knc_cfo_f32(x32, (size_t)B, (size_t)n, (float *)out);
        else if (!strcmp(o.kernel, "xcorr"))
            st = f64 ? knc_xcorr_batch_f64((double *)x, (size_t)B, (size_t)n, (double *)x, (size_t)o.xlen, (double *)out)
                     : knc_xcorr_batch_f32(x32, (size_t)B, (size_t)n, x32, (size_t)o.xlen, (float *)out);
        else if (!strcmp(o.kernel, "psd"))
            st = f64 ? knc_psd_f64(&plan, (double *)x, (size_t)B, KNC_WIN_HANN, (double *)work, (double *)out)
                     : knc_psd_f32(&plan, x32, (size_t)B, KNC_WIN_HANN, (float *)work, (float *)out);
        else if (!strcmp(o.kernel, "fft"))
            st = f64 ? knc_fft_batch_f64(&plan, (double *)work, (size_t)B)
                     : knc_fft_batch_f32(&plan, (float *)work, (size_t)B);
        else if (!strcmp(o.kernel, "features"))
            st = knc_features_batch(x32, (size_t)B, (size_t)n, (double *)out);
        else
            die("unknown kernel");
        if (r >= 0)
            times[r] = knc_now() - ts;
        if (st != 0)
            die("kernel returned an error");
    }
    if (!strcmp(o.kernel, "fft"))
        memcpy(out, work, (size_t)(2 * B * n) * esz);

    /* ---- post: error vs double reference, metadata */
    t0 = knc_now();
    err = check(o.kernel, f64 || !strcmp(o.kernel, "features"), x32, y32, out, B, n, o.xlen);
    if (o.dataset && knc_sha256_file(o.dataset, sha) != 0)
        die("cannot hash dataset");
    t_post = knc_now() - t0;

    memcpy(sorted, times, (size_t)o.reps * sizeof(double));
    qsort(sorted, (size_t)o.reps, sizeof(double), cmpd);
    cpu_info(cpu, sizeof cpu, &hyper);
    if (uname(&un) != 0)
        memset(&un, 0, sizeof un);
    strftime(tbuf, sizeof tbuf, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    if (o.out && !(fo = fopen(o.out, "w")))
        die("cannot open --out file");
    fprintf(fo, "{\"schema_version\": \"1.0\", \"status\": \"ok\", \"simulated\": false, ");
    fprintf(fo, "\"execution_mode\": \"%s\", \"kernel\": \"%s\", \"precision\": \"%s\", ",
            KNC_EXEC_MODE, o.kernel, o.precision);
    fprintf(fo, "\"batch\": %ld, \"samples_per_example\": %ld, \"threads\": %ld, \"warmup\": %ld, \"reps\": %ld, ",
            B, n, o.threads, o.warmup, o.reps);
    if (!strcmp(o.kernel, "xcorr"))
        fprintf(fo, "\"xcorr_len\": %ld, ", o.xlen);
    fprintf(fo, "\"input\": {\"kind\": \"%s\", \"path\": ", o.dataset ? "dataset" : "synthetic");
    if (o.dataset) jstr(fo, o.dataset); else fprintf(fo, "null");
    fprintf(fo, ", \"sha256\": ");
    if (o.dataset) jstr(fo, sha); else fprintf(fo, "null");
    fprintf(fo, ", \"wrapped\": %s}, ", wrapped ? "true" : "false");
    fprintf(fo, "\"timing\": {\"unit\": \"s\", \"init\": "); jnum(fo, t_init);
    fprintf(fo, ", \"load\": "); jnum(fo, t_load);
    fprintf(fo, ", \"transfer\": null, \"kernel\": {\"median\": "); jnum(fo, pct(sorted, o.reps, 0.5));
    fprintf(fo, ", \"min\": "); jnum(fo, sorted[0]);
    fprintf(fo, ", \"max\": "); jnum(fo, sorted[o.reps - 1]);
    fprintf(fo, ", \"q25\": "); jnum(fo, pct(sorted, o.reps, 0.25));
    fprintf(fo, ", \"q75\": "); jnum(fo, pct(sorted, o.reps, 0.75));
    fprintf(fo, ", \"iqr\": "); jnum(fo, pct(sorted, o.reps, 0.75) - pct(sorted, o.reps, 0.25));
    fprintf(fo, ", \"samples\": [");
    for (r = 0; r < o.reps; r++) {
        if (r) fprintf(fo, ", ");
        jnum(fo, times[r]);
    }
    fprintf(fo, "]}, \"post\": "); jnum(fo, t_post);
    fprintf(fo, "}, \"throughput\": {\"complex_samples_per_s\": ");
    jnum(fo, (double)(B * n) / pct(sorted, o.reps, 0.5));
    fprintf(fo, ", \"examples_per_s\": "); jnum(fo, (double)B / pct(sorted, o.reps, 0.5));
    fprintf(fo, "}, \"error\": ");
    if (err.metric) {
        fprintf(fo, "{\"reference\": \"naive double-precision C loop\", \"examples_checked\": %ld, \"metric\": ",
                B < NREF ? B : NREF);
        jstr(fo, err.metric);
        fprintf(fo, ", \"max_abs\": "); jnum(fo, err.max_abs);
        fprintf(fo, ", \"max_rel\": "); jnum(fo, err.max_rel);
        fprintf(fo, "}, ");
    } else {
        fprintf(fo, "{\"status\": \"unavailable\", \"reason\": \"no independent C reference; "
                    "features are checked against NumPy in tests/test_c_core.py\"}, ");
    }
    fprintf(fo, "\"metadata\": {\"timestamp_utc\": \"%s\", \"cpu_model\": ", tbuf);
    jstr(fo, cpu);
    fprintf(fo, ", \"cpu_hypervisor_flag\": %s, \"online_cpus\": %ld, \"os\": {\"sysname\": ",
            hyper ? "true" : "false", sysconf(_SC_NPROCESSORS_ONLN));
    jstr(fo, un.sysname);
    fprintf(fo, ", \"release\": "); jstr(fo, un.release);
    fprintf(fo, ", \"machine\": "); jstr(fo, un.machine);
    fprintf(fo, "}, \"compiler\": ");
#if defined(__INTEL_COMPILER)
    fprintf(fo, "\"icc %d.%d\"", __INTEL_COMPILER, __INTEL_COMPILER_UPDATE);
#elif defined(__clang__)
    fprintf(fo, "\"clang %s\"", __clang_version__);
#elif defined(__GNUC__)
    fprintf(fo, "\"gcc %s\"", __VERSION__);
#else
    fprintf(fo, "\"unknown\"");
#endif
    fprintf(fo, ", \"build_flags\": "); jstr(fo, KNC_BUILD_FLAGS);
#ifdef _OPENMP
    fprintf(fo, ", \"openmp\": {\"version\": %d, \"max_threads\": %d", _OPENMP, omp_get_max_threads());
#else
    fprintf(fo, ", \"openmp\": {\"version\": null, \"max_threads\": 1");
#endif
    {
        static const char *envs[] = {"OMP_NUM_THREADS", "OMP_PROC_BIND", "OMP_PLACES",
                                     "KMP_AFFINITY", "KMP_PLACE_THREADS", "KMP_HW_SUBSET"};
        size_t e;
        fprintf(fo, ", \"env\": {");
        for (e = 0; e < sizeof envs / sizeof envs[0]; e++) {
            const char *v = getenv(envs[e]);
            fprintf(fo, "%s\"%s\": ", e ? ", " : "", envs[e]);
            if (v) jstr(fo, v); else fprintf(fo, "null");
        }
        fprintf(fo, "}}");
    }
    fprintf(fo, ", \"git_commit\": "); jstr(fo, KNC_GIT_COMMIT);
    fprintf(fo, "}}\n");
    if (fo != stdout)
        fclose(fo);

    if (have_plan)
        knc_fft_plan_free(&plan);
    if (o.dataset)
        kncds_close(&ds);
    if (f64 && x != (void *)x32)
        free(x);
    if (y && y != (void *)y32)
        free(y);
    free(y32);
    free(x32);
    free(out);
    free(work);
    free(times);
    free(sorted);
    return rc;
}
