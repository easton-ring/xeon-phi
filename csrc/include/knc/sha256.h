#ifndef KNC_SHA256_H
#define KNC_SHA256_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char buf[64];
    size_t buflen;
} knc_sha256_ctx;

void knc_sha256_init(knc_sha256_ctx *c);
void knc_sha256_update(knc_sha256_ctx *c, const void *data, size_t len);
void knc_sha256_final(knc_sha256_ctx *c, unsigned char out[32]);
/* SHA-256 of a whole file as 64 lowercase hex chars + NUL. Returns 0 on success. */
int knc_sha256_file(const char *path, char hex[65]);

#endif
