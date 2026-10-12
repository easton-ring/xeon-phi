/* Minimal test helpers for plain-executable CTest tests (no frameworks). */
#ifndef KNC_CHECK_H
#define KNC_CHECK_H
#include <math.h>
#include <stdio.h>

static int g_fail = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_fail++;                                                            \
        }                                                                        \
    } while (0)
#define TEST_EXIT() (g_fail ? (fprintf(stderr, "%d failure(s)\n", g_fail), 1) : (printf("ok\n"), 0))

/* Deterministic uniform [-1, 1) generator (LCG, 32-bit state). */
static unsigned long g_lcg = 12345u;
static inline double urand(void)
{
    g_lcg = (g_lcg * 1103515245u + 12345u) & 0x7fffffffu;
    return (double)g_lcg / 1073741824.0 - 1.0;
}
#endif
