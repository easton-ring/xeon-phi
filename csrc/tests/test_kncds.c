/* C reader tests: build a valid file in memory, then corrupt it one way at a time. */
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "knc/crc32.h"
#include "knc/kncds.h"

#define N 5
#define S 24
#define LAB_END (64 + 8 * N)
#define PAY_OFF (((LAB_END) + 63) / 64 * 64)
#define FILE_LEN (PAY_OFF + N * S * 8)

static unsigned char buf[FILE_LEN + 64];

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}
static void put64(unsigned char *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}
static void fix_header_crc(void)
{
    memset(buf + 60, 0, 4);
    put32(buf + 60, knc_crc32_update(0, buf, 64));
}
static void fix_data_crc(void)
{
    uint32_t crc = knc_crc32_update(0, buf + 64, 8 * N);
    crc = knc_crc32_update(crc, buf + PAY_OFF, N * S * 8);
    put32(buf + 56, crc);
    fix_header_crc();
}

static void build(void)
{
    int i;
    memset(buf, 0, sizeof buf);
    memcpy(buf, "KNCRFDS\0", 8);
    buf[8] = 1; /* version 1.0 */
    put32(buf + 12, 64);
    put64(buf + 16, N);
    put32(buf + 24, S);
    put32(buf + 28, 3);
    put32(buf + 32, 1);
    put64(buf + 40, 64);
    put64(buf + 48, PAY_OFF);
    for (i = 0; i < N; i++) {
        float snr = (float)i - 2.5f;
        uint32_t bits;
        memcpy(&bits, &snr, 4);
        put32(buf + 64 + 8 * i, (uint32_t)(i % 3));
        put32(buf + 64 + 8 * i + 4, bits);
    }
    for (i = 0; i < N * S * 2; i++) {
        float v = (float)i * 0.25f;
        uint32_t bits;
        memcpy(&bits, &v, 4);
        put32(buf + PAY_OFF + 4 * i, bits);
    }
    fix_data_crc();
}

static int open_bytes(size_t len)
{
    kncds_file f;
    int rc;
    FILE *fp = fopen("test_kncds.tmp", "wb");
    fwrite(buf, 1, len, fp);
    fclose(fp);
    rc = kncds_open(&f, "test_kncds.tmp");
    if (rc == KNCDS_OK)
        kncds_close(&f);
    return rc;
}

int main(void)
{
    kncds_file f;
    float *x = (float *)malloc(N * S * 2 * sizeof(float));
    int i;

    build();
    CHECK(open_bytes(FILE_LEN) == KNCDS_OK);
    CHECK(kncds_open(&f, "test_kncds.tmp") == KNCDS_OK);
    CHECK(f.h.num_examples == N && f.h.samples_per_example == S && f.h.num_classes == 3);
    CHECK(f.class_id[4] == 1 && f.snr_db[0] == -2.5f);
    CHECK(kncds_read(&f, 1, 3, x) == KNCDS_OK);
    CHECK(x[0] == (float)(S * 2) * 0.25f);
    CHECK(x[3 * S * 2 - 1] == (float)(4 * S * 2 - 1) * 0.25f);
    CHECK(kncds_read(&f, 4, 2, x) == KNCDS_E_ARG);
    CHECK(kncds_read(&f, UINT64_MAX, 2, x) == KNCDS_E_ARG);
    kncds_close(&f);

    CHECK(open_bytes(40) == KNCDS_E_TRUNCATED);
    build(); buf[7] = 'X';                       CHECK(open_bytes(FILE_LEN) == KNCDS_E_MAGIC);
    build(); buf[8] = 2; fix_header_crc();       CHECK(open_bytes(FILE_LEN) == KNCDS_E_VERSION);
    build(); buf[10] = 7; fix_header_crc();      CHECK(open_bytes(FILE_LEN) == KNCDS_OK); /* minor ok */
    build(); put32(buf + 12, 128); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_HEADER_SIZE);
    build(); buf[28] = 9;                        CHECK(open_bytes(FILE_LEN) == KNCDS_E_HEADER_CRC);
    build(); put32(buf + 32, 2); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_FIELD);
    build(); put32(buf + 36, 4); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_FIELD);
    build(); put32(buf + 24, 0); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_FIELD);
    build(); put64(buf + 16, ((uint64_t)1 << 62) + 1); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_OVERFLOW);
    build(); put64(buf + 16, UINT64_MAX / (S * 8) + 1); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_OVERFLOW);
    build(); put64(buf + 48, UINT64_MAX - 63); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_OVERFLOW);
    build(); put64(buf + 48, (uint64_t)1 << 40); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_OFFSET);
    build(); put64(buf + 40, (uint64_t)1 << 40); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_OFFSET);
    build(); put64(buf + 48, PAY_OFF + 8); fix_header_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_ALIGN);
    build(); put64(buf + 48, PAY_OFF + 64); fix_header_crc(); CHECK(open_bytes(FILE_LEN + 64) == KNCDS_E_LAYOUT);
    build(); buf[LAB_END] = 1;                   CHECK(open_bytes(FILE_LEN) == KNCDS_E_LAYOUT); /* padding */
    build();                                     CHECK(open_bytes(FILE_LEN - 1) == KNCDS_E_TRUNCATED);
    build();                                     CHECK(open_bytes(FILE_LEN + 3) == KNCDS_E_TRAILING);
    build(); buf[FILE_LEN - 2] ^= 1;             CHECK(open_bytes(FILE_LEN) == KNCDS_E_DATA_CRC);
    build(); buf[64 + 5] ^= 1;                   CHECK(open_bytes(FILE_LEN) == KNCDS_E_DATA_CRC);
    build(); put32(buf + 64, 3); fix_data_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_LABEL);
    build(); put32(buf + 64, 0xFFFFFFFFu); fix_data_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_LABEL);
    build(); put32(buf + 68, 0x7FC00000u); fix_data_crc(); CHECK(open_bytes(FILE_LEN) == KNCDS_E_LABEL);
    CHECK(kncds_open(&f, "does/not/exist.kncds") == KNCDS_E_IO);
    for (i = 0; i <= KNCDS_E_ARG; i++)
        CHECK(kncds_strerror(i)[0] == 'O' || kncds_strerror(i)[0] == 'E');
    remove("test_kncds.tmp");
    free(x);
    return TEST_EXIT();
}
