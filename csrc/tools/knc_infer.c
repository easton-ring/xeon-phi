/* knc_infer --model M.txt --dataset D.kncds --out P.csv [--chunk N]
 * C features + C forward pass for every example; writes
 * "index,true,pred,margin,logit_0,...". Prints accuracy to stderr. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knc/features.h"
#include "knc/kncds.h"
#include "knc/model.h"

int main(int argc, char **argv)
{
    const char *mpath = NULL, *dpath = NULL, *opath = NULL;
    long chunk = 256, i, j, correct = 0;
    uint64_t first;
    kncds_file ds;
    knc_model m;
    char err[256];
    float *x;
    double *feat, *scratch, *logits;
    FILE *fo;
    int st;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--model") && i + 1 < argc) mpath = argv[++i];
        else if (!strcmp(argv[i], "--dataset") && i + 1 < argc) dpath = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) opath = argv[++i];
        else if (!strcmp(argv[i], "--chunk") && i + 1 < argc) chunk = strtol(argv[++i], NULL, 10);
        else {
            fprintf(stderr, "usage: %s --model M.txt --dataset D.kncds --out P.csv [--chunk N]\n", argv[0]);
            return 2;
        }
    }
    if (!mpath || !dpath || !opath || chunk < 1) {
        fprintf(stderr, "knc_infer: --model, --dataset and --out are required\n");
        return 2;
    }
    if (knc_model_load(&m, mpath, err, sizeof err) != 0) {
        fprintf(stderr, "knc_infer: %s\n", err);
        return 1;
    }
    if (m.n_features != KNC_NUM_FEATURES) {
        fprintf(stderr, "knc_infer: model has %d features, C core computes %d\n", m.n_features, KNC_NUM_FEATURES);
        return 1;
    }
    st = kncds_open(&ds, dpath);
    if (st != KNCDS_OK) {
        fprintf(stderr, "knc_infer: %s: %s %s\n", dpath, kncds_strerror(st), ds.msg);
        return 1;
    }
    if ((long)ds.h.num_classes != m.n_classes) {
        fprintf(stderr, "knc_infer: dataset has %lu classes, model %d\n", (unsigned long)ds.h.num_classes, m.n_classes);
        return 1;
    }
    x = (float *)malloc((size_t)chunk * ds.h.samples_per_example * 2 * sizeof(float));
    feat = (double *)malloc((size_t)chunk * KNC_NUM_FEATURES * sizeof(double));
    scratch = (double *)malloc((size_t)(2 * m.max_width) * sizeof(double));
    logits = (double *)malloc((size_t)m.n_classes * sizeof(double));
    fo = fopen(opath, "w");
    if (!x || !feat || !scratch || !logits || !fo) {
        fprintf(stderr, "knc_infer: allocation or --out failed\n");
        return 1;
    }
    fprintf(fo, "index,true,pred,margin");
    for (j = 0; j < m.n_classes; j++)
        fprintf(fo, ",logit_%ld", j);
    fprintf(fo, "\n");
    for (first = 0; first < ds.h.num_examples; first += (uint64_t)chunk) {
        uint64_t cnt = ds.h.num_examples - first < (uint64_t)chunk ? ds.h.num_examples - first : (uint64_t)chunk;
        if (kncds_read(&ds, first, cnt, x) != KNCDS_OK ||
            knc_features_batch(x, (size_t)cnt, ds.h.samples_per_example, feat) != KNC_OK) {
            fprintf(stderr, "knc_infer: read/feature failure at %lu\n", (unsigned long)first);
            return 1;
        }
        for (i = 0; i < (long)cnt; i++) {
            int p = knc_model_predict(&m, feat + i * KNC_NUM_FEATURES, scratch, logits);
            double second = -1e308;
            long idx = (long)first + i;
            for (j = 0; j < m.n_classes; j++)
                if (j != p && logits[j] > second)
                    second = logits[j];
            correct += p == ds.class_id[idx];
            fprintf(fo, "%ld,%d,%d,%.17g", idx, (int)ds.class_id[idx], p, logits[p] - second);
            for (j = 0; j < m.n_classes; j++)
                fprintf(fo, ",%.17g", logits[j]);
            fprintf(fo, "\n");
        }
    }
    fclose(fo);
    fprintf(stderr, "knc_infer: %lu examples, accuracy %.4f\n", (unsigned long)ds.h.num_examples,
            ds.h.num_examples ? (double)correct / (double)ds.h.num_examples : 0.0);
    kncds_close(&ds);
    knc_model_free(&m);
    free(x);
    free(feat);
    free(scratch);
    free(logits);
    return 0;
}
