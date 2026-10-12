/* KNCRFDS v1 reader: validates in the order documented in docs/dataset-format.md. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "knc/crc32.h"
#include "knc/kncds.h"

static const char *const names[] = {
    "OK", "E_IO", "E_MAGIC", "E_VERSION", "E_HEADER_SIZE", "E_HEADER_CRC", "E_FIELD",
    "E_OVERFLOW", "E_LAYOUT", "E_OFFSET", "E_ALIGN", "E_TRUNCATED", "E_TRAILING",
    "E_DATA_CRC", "E_LABEL", "E_NOMEM", "E_ARG"};

const char *kncds_strerror(int code)
{
    if (code < 0 || code >= (int)(sizeof names / sizeof names[0]))
        return "E_UNKNOWN";
    return names[code];
}

static int fail(char *msg, size_t len, int code, const char *fmt, ...)
{
    va_list ap;
    if (msg && len) {
        va_start(ap, fmt);
        vsnprintf(msg, len, fmt, ap);
        va_end(ap);
    }
    return code;
}

static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const unsigned char *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

/* Checked u64 arithmetic: return 0 on overflow. */
static int mul64(uint64_t a, uint64_t b, uint64_t *r)
{
    if (a != 0 && b > UINT64_MAX / a)
        return 0;
    *r = a * b;
    return 1;
}
static int add64(uint64_t a, uint64_t b, uint64_t *r)
{
    if (b > UINT64_MAX - a)
        return 0;
    *r = a + b;
    return 1;
}

int kncds_check_header(const unsigned char raw[64], size_t raw_len, uint64_t file_size,
                       kncds_header *h, char *msg, size_t msglen)
{
    unsigned char tmp[64];
    uint64_t labels_bytes, labels_end, example_bytes, payload_bytes, payload_end, aligned;

    if (raw_len < KNCDS_HEADER_SIZE)
        return fail(msg, msglen, KNCDS_E_TRUNCATED, "file is %lu bytes, header needs 64",
                    (unsigned long)raw_len);
    if (memcmp(raw, "KNCRFDS\0", 8) != 0)
        return fail(msg, msglen, KNCDS_E_MAGIC, "bad magic");
    h->version_major = rd16(raw + 8);
    h->version_minor = rd16(raw + 10);
    h->header_size = rd32(raw + 12);
    h->num_examples = rd64(raw + 16);
    h->samples_per_example = rd32(raw + 24);
    h->num_classes = rd32(raw + 28);
    h->sample_format = rd32(raw + 32);
    h->flags = rd32(raw + 36);
    h->labels_offset = rd64(raw + 40);
    h->payload_offset = rd64(raw + 48);
    h->data_crc32 = rd32(raw + 56);
    h->header_crc32 = rd32(raw + 60);

    if (h->version_major != KNCDS_VERSION_MAJOR)
        return fail(msg, msglen, KNCDS_E_VERSION, "unsupported version %u.%u",
                    (unsigned)h->version_major, (unsigned)h->version_minor);
    if (h->header_size != KNCDS_HEADER_SIZE)
        return fail(msg, msglen, KNCDS_E_HEADER_SIZE, "header_size %lu != 64",
                    (unsigned long)h->header_size);
    memcpy(tmp, raw, 64);
    memset(tmp + 60, 0, 4);
    if (knc_crc32_update(0, tmp, 64) != h->header_crc32)
        return fail(msg, msglen, KNCDS_E_HEADER_CRC, "header CRC mismatch");
    if (h->sample_format != KNCDS_SAMPLE_FORMAT_CF32)
        return fail(msg, msglen, KNCDS_E_FIELD, "unknown sample_format %lu",
                    (unsigned long)h->sample_format);
    if (h->flags != 0)
        return fail(msg, msglen, KNCDS_E_FIELD, "unknown flags 0x%lx", (unsigned long)h->flags);
    if (h->samples_per_example == 0 || h->num_classes == 0)
        return fail(msg, msglen, KNCDS_E_FIELD, "samples_per_example and num_classes must be > 0");
    if (!mul64(h->num_examples, 8, &labels_bytes) ||
        !add64(h->labels_offset, labels_bytes, &labels_end) ||
        !mul64(h->samples_per_example, 8, &example_bytes) ||
        !mul64(h->num_examples, example_bytes, &payload_bytes) ||
        !add64(h->payload_offset, payload_bytes, &payload_end))
        return fail(msg, msglen, KNCDS_E_OVERFLOW, "size/offset arithmetic overflows u64");
    if (h->labels_offset > file_size || h->payload_offset > file_size)
        return fail(msg, msglen, KNCDS_E_OFFSET, "section offset past end of file");
    if (h->payload_offset % KNCDS_PAYLOAD_ALIGN != 0)
        return fail(msg, msglen, KNCDS_E_ALIGN, "payload_offset not 64-byte aligned");
    if (!add64(labels_end, KNCDS_PAYLOAD_ALIGN - 1, &aligned))
        return fail(msg, msglen, KNCDS_E_OVERFLOW, "size/offset arithmetic overflows u64");
    aligned -= aligned % KNCDS_PAYLOAD_ALIGN;
    if (h->labels_offset != KNCDS_HEADER_SIZE || h->payload_offset != aligned)
        return fail(msg, msglen, KNCDS_E_LAYOUT, "unexpected section layout");
    if (payload_end > file_size)
        return fail(msg, msglen, KNCDS_E_TRUNCATED, "payload ends past end of file");
    if (payload_end < file_size)
        return fail(msg, msglen, KNCDS_E_TRAILING, "unexpected trailing bytes");
    return KNCDS_OK;
}

static int host_is_le(void)
{
    const uint16_t one = 1;
    return *(const unsigned char *)&one == 1;
}

static int read_exact(FILE *fp, void *buf, size_t n)
{
    return fread(buf, 1, n, fp) == n;
}

int kncds_open(kncds_file *f, const char *path)
{
    unsigned char raw[64], *lab = NULL, *chunk = NULL;
    size_t got, nlab, i, pad;
    uint64_t remaining;
    uint32_t crc;
    off_t end;
    int rc;

    memset(f, 0, sizeof *f);
    f->fp = fopen(path, "rb");
    if (!f->fp)
        return fail(f->msg, sizeof f->msg, KNCDS_E_IO, "cannot open %s", path);
    if (fseeko(f->fp, 0, SEEK_END) != 0 || (end = ftello(f->fp)) < 0 || fseeko(f->fp, 0, SEEK_SET) != 0) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_IO, "cannot determine file size");
        goto out;
    }
    f->file_size = (uint64_t)end;
    got = fread(raw, 1, 64, f->fp);
    rc = kncds_check_header(raw, got, f->file_size, &f->h, f->msg, sizeof f->msg);
    if (rc != KNCDS_OK)
        goto out;

    /* Labels (validated layout guarantees they fit in the file). */
    if (f->h.num_examples > (uint64_t)((size_t)-1) / 8u) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_NOMEM, "labels do not fit in memory");
        goto out;
    }
    nlab = (size_t)f->h.num_examples;
    lab = (unsigned char *)malloc(nlab * 8 + 1);
    f->class_id = (int32_t *)malloc(nlab * sizeof(int32_t) + 1);
    f->snr_db = (float *)malloc(nlab * sizeof(float) + 1);
    chunk = (unsigned char *)malloc(1u << 20);
    if (!lab || !f->class_id || !f->snr_db || !chunk) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_NOMEM, "out of memory");
        goto out;
    }
    if (!read_exact(f->fp, lab, nlab * 8)) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_IO, "short read in labels");
        goto out;
    }
    crc = knc_crc32_update(0, lab, nlab * 8);
    pad = (size_t)(f->h.payload_offset - f->h.labels_offset - (uint64_t)nlab * 8);
    if (!read_exact(f->fp, chunk, pad)) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_IO, "short read in padding");
        goto out;
    }
    for (i = 0; i < pad; i++)
        if (chunk[i] != 0) {
            rc = fail(f->msg, sizeof f->msg, KNCDS_E_LAYOUT, "non-zero padding before payload");
            goto out;
        }
    remaining = f->file_size - f->h.payload_offset;
    while (remaining > 0) {
        size_t want = remaining > (1u << 20) ? (size_t)(1u << 20) : (size_t)remaining;
        if (!read_exact(f->fp, chunk, want)) {
            rc = fail(f->msg, sizeof f->msg, KNCDS_E_IO, "short read in payload");
            goto out;
        }
        crc = knc_crc32_update(crc, chunk, want);
        remaining -= want;
    }
    if (crc != f->h.data_crc32) {
        rc = fail(f->msg, sizeof f->msg, KNCDS_E_DATA_CRC, "labels+payload CRC mismatch");
        goto out;
    }
    for (i = 0; i < nlab; i++) {
        uint32_t bits = rd32(lab + 8 * i + 4);
        int32_t cid = (int32_t)rd32(lab + 8 * i);
        float snr;
        memcpy(&snr, &bits, 4);
        if (cid < 0 || (uint32_t)cid >= f->h.num_classes) {
            rc = fail(f->msg, sizeof f->msg, KNCDS_E_LABEL, "class_id %ld out of range at %lu",
                      (long)cid, (unsigned long)i);
            goto out;
        }
        if (!isfinite(snr)) {
            rc = fail(f->msg, sizeof f->msg, KNCDS_E_LABEL, "non-finite snr_db at %lu", (unsigned long)i);
            goto out;
        }
        f->class_id[i] = cid;
        f->snr_db[i] = snr;
    }
    rc = KNCDS_OK;
out:
    free(lab);
    free(chunk);
    if (rc != KNCDS_OK)
        kncds_close(f);
    return rc;
}

int kncds_read(kncds_file *f, uint64_t first, uint64_t count, float *dst)
{
    uint64_t end, off, nbytes;
    size_t nfloats, i;
    unsigned char *p = (unsigned char *)dst;

    if (!f || !f->fp || !dst || !add64(first, count, &end) || end > f->h.num_examples)
        return KNCDS_E_ARG;
    nbytes = count * f->h.samples_per_example * 8u; /* bounded by validated file size */
    off = f->h.payload_offset + first * f->h.samples_per_example * 8u;
    if (nbytes > (uint64_t)((size_t)-1))
        return KNCDS_E_NOMEM;
    if (fseeko(f->fp, (off_t)off, SEEK_SET) != 0 || !read_exact(f->fp, dst, (size_t)nbytes))
        return KNCDS_E_IO;
    /* Payload is little-endian float32; byte-swap only on big-endian hosts
     * (x86-64 and KNC are little-endian, so this loop never runs there). */
    nfloats = (size_t)(nbytes / 4);
    for (i = 0; i < nfloats && !host_is_le(); i++) {
        uint32_t bits = rd32(p + 4 * i);
        memcpy(p + 4 * i, &bits, 4);
    }
    return KNCDS_OK;
}

void kncds_close(kncds_file *f)
{
    if (f->fp)
        fclose(f->fp);
    free(f->class_id);
    free(f->snr_db);
    f->fp = NULL;
    f->class_id = NULL;
    f->snr_db = NULL;
}
