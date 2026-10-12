#include <string.h>

#include "check.h"
#include "knc/crc32.h"
#include "knc/sha256.h"

static void hex(const unsigned char *md, char *out)
{
    int i;
    for (i = 0; i < 32; i++)
        sprintf(out + 2 * i, "%02x", md[i]);
}

int main(void)
{
    unsigned char md[32];
    char h[65];
    knc_sha256_ctx c;
    const char *m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint32_t crc;

    CHECK(knc_crc32_update(0, "123456789", 9) == 0xCBF43926u);
    crc = knc_crc32_update(0, "1234", 4);
    CHECK(knc_crc32_update(crc, "56789", 5) == 0xCBF43926u);
    CHECK(knc_crc32_update(0, "", 0) == 0u);

    knc_sha256_init(&c);
    knc_sha256_final(&c, md);
    hex(md, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    knc_sha256_init(&c);
    knc_sha256_update(&c, "abc", 3);
    knc_sha256_final(&c, md);
    hex(md, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    knc_sha256_init(&c);
    knc_sha256_update(&c, m2, strlen(m2));
    knc_sha256_final(&c, md);
    hex(md, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    return TEST_EXIT();
}
