#ifndef KNC_KNCDS_H
#define KNC_KNCDS_H
/* KNCRFDS v1 dataset reader. Format and check order: docs/dataset-format.md. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum {
    KNCDS_OK = 0,
    KNCDS_E_IO,
    KNCDS_E_MAGIC,
    KNCDS_E_VERSION,
    KNCDS_E_HEADER_SIZE,
    KNCDS_E_HEADER_CRC,
    KNCDS_E_FIELD,
    KNCDS_E_OVERFLOW,
    KNCDS_E_LAYOUT,
    KNCDS_E_OFFSET,
    KNCDS_E_ALIGN,
    KNCDS_E_TRUNCATED,
    KNCDS_E_TRAILING,
    KNCDS_E_DATA_CRC,
    KNCDS_E_LABEL,
    KNCDS_E_NOMEM,
    KNCDS_E_ARG
};

#define KNCDS_HEADER_SIZE 64u
#define KNCDS_PAYLOAD_ALIGN 64u
#define KNCDS_VERSION_MAJOR 1u
#define KNCDS_SAMPLE_FORMAT_CF32 1u

typedef struct {
    uint16_t version_major, version_minor;
    uint32_t header_size;
    uint64_t num_examples;
    uint32_t samples_per_example, num_classes, sample_format, flags;
    uint64_t labels_offset, payload_offset;
    uint32_t data_crc32, header_crc32;
} kncds_header;

typedef struct {
    FILE *fp;
    kncds_header h;
    uint64_t file_size;
    int32_t *class_id; /* num_examples entries */
    float *snr_db;     /* num_examples entries */
    char msg[256];     /* human-readable detail of the last error */
} kncds_file;

/* Symbolic name of a status code, e.g. "E_MAGIC" ("OK" for 0). */
const char *kncds_strerror(int code);

/* Validate a 64-byte header against the file size (no data CRC). */
int kncds_check_header(const unsigned char raw[64], size_t raw_len, uint64_t file_size,
                       kncds_header *h, char *msg, size_t msglen);

/* Open and fully validate a file (header, layout, data CRC, labels).
 * On failure the file is closed and f->msg describes the error. */
int kncds_open(kncds_file *f, const char *path);

/* Read examples [first, first+count) into dst (count*S*2 floats, I/Q interleaved). */
int kncds_read(kncds_file *f, uint64_t first, uint64_t count, float *dst);

void kncds_close(kncds_file *f);

#endif
