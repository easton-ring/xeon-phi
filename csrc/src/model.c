/* Loader and forward pass for "KNCMODEL 1" dense classifiers. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knc/model.h"

static int read_vec(FILE *fp, double *v, long n)
{
    long i;
    for (i = 0; i < n; i++)
        if (fscanf(fp, "%lf", &v[i]) != 1)
            return 0;
    return 1;
}

int knc_model_load(knc_model *m, const char *path, char *err, size_t errlen)
{
    char tag[16];
    int ver, l, width;
    FILE *fp = fopen(path, "r");
    memset(m, 0, sizeof *m);
    if (!fp) {
        snprintf(err, errlen, "cannot open %s", path);
        return 1;
    }
    if (fscanf(fp, "%15s %d", tag, &ver) != 2 || strcmp(tag, "KNCMODEL") != 0 || ver != 1 ||
        fscanf(fp, "%d %d %d", &m->n_features, &m->n_classes, &m->n_layers) != 3 ||
        m->n_features <= 0 || m->n_classes <= 0 || m->n_layers <= 0 ||
        m->n_layers > KNC_MODEL_MAX_LAYERS || m->n_features > 100000) {
        snprintf(err, errlen, "bad model header");
        goto fail;
    }
    m->mean = (double *)malloc((size_t)m->n_features * sizeof(double));
    m->scale = (double *)malloc((size_t)m->n_features * sizeof(double));
    if (!m->mean || !m->scale || !read_vec(fp, m->mean, m->n_features) ||
        !read_vec(fp, m->scale, m->n_features)) {
        snprintf(err, errlen, "bad standardization block");
        goto fail;
    }
    width = m->n_features;
    m->max_width = width;
    for (l = 0; l < m->n_layers; l++) {
        knc_layer *L = &m->layers[l];
        if (fscanf(fp, "%d %d %d", &L->in, &L->out, &L->act) != 3 || L->in != width ||
            L->out <= 0 || L->out > 100000 || (L->act != 0 && L->act != 1)) {
            snprintf(err, errlen, "bad layer %d header", l);
            goto fail;
        }
        L->W = (double *)malloc((size_t)L->in * (size_t)L->out * sizeof(double));
        L->b = (double *)malloc((size_t)L->out * sizeof(double));
        if (!L->W || !L->b || !read_vec(fp, L->W, (long)L->in * L->out) || !read_vec(fp, L->b, L->out)) {
            snprintf(err, errlen, "bad layer %d weights", l);
            goto fail;
        }
        width = L->out;
        if (width > m->max_width)
            m->max_width = width;
    }
    if (width != m->n_classes) {
        snprintf(err, errlen, "last layer width %d != n_classes %d", width, m->n_classes);
        goto fail;
    }
    fclose(fp);
    return 0;
fail:
    fclose(fp);
    knc_model_free(m);
    return 1;
}

void knc_model_free(knc_model *m)
{
    int l;
    free(m->mean);
    free(m->scale);
    for (l = 0; l < KNC_MODEL_MAX_LAYERS; l++) {
        free(m->layers[l].W);
        free(m->layers[l].b);
    }
    memset(m, 0, sizeof *m);
}

int knc_model_predict(const knc_model *m, const double *feat, double *scratch, double *logits)
{
    double *a = scratch, *z = scratch + m->max_width, *t;
    int i, j, l, best = 0;
    for (i = 0; i < m->n_features; i++)
        a[i] = (feat[i] - m->mean[i]) / m->scale[i];
    for (l = 0; l < m->n_layers; l++) {
        const knc_layer *L = &m->layers[l];
        for (j = 0; j < L->out; j++) {
            const double *w = L->W + (size_t)j * L->in;
            double s = L->b[j];
            for (i = 0; i < L->in; i++)
                s += w[i] * a[i];
            z[j] = (L->act == 1 && s < 0) ? 0.0 : s;
        }
        t = a; a = z; z = t;
    }
    for (j = 0; j < m->n_classes; j++) {
        logits[j] = a[j];
        if (a[j] > a[best])
            best = j;
    }
    return best;
}
