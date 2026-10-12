/* knc_validate FILE...: validate KNCRFDS files with the C reader.
 * Prints one JSON object per file; exit status 1 if any file is invalid. */
#include <stdio.h>

#include "knc/kncds.h"
#include "knc/sha256.h"

static void json_str(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        if ((unsigned char)*s >= 0x20)
            putchar(*s);
    }
    putchar('"');
}

int main(int argc, char **argv)
{
    int i, rc = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE.kncds...\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; i++) {
        kncds_file f;
        int st = kncds_open(&f, argv[i]);
        printf("{\"file\": ");
        json_str(argv[i]);
        if (st != KNCDS_OK) {
            printf(", \"status\": \"error\", \"code\": \"%s\", \"message\": ", kncds_strerror(st));
            json_str(f.msg);
            printf("}\n");
            rc = 1;
        } else {
            char hex[65];
            if (knc_sha256_file(argv[i], hex) != 0)
                hex[0] = '\0';
            printf(", \"status\": \"ok\", \"num_examples\": %llu, \"samples_per_example\": %lu, "
                   "\"num_classes\": %lu, \"sha256\": \"%s\"}\n",
                   (unsigned long long)f.h.num_examples, (unsigned long)f.h.samples_per_example,
                   (unsigned long)f.h.num_classes, hex);
            kncds_close(&f);
        }
    }
    return rc;
}
