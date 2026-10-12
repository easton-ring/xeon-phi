#ifndef KNC_MODEL_H
#define KNC_MODEL_H
/* Dense feed-forward classifier exported from scikit-learn
 * (logistic regression = 1 identity layer; MLP = ReLU hidden layers).
 * File format "KNCMODEL 1" text: docs/features.md#model-export. */
#include <stddef.h>

#define KNC_MODEL_MAX_LAYERS 8

typedef struct {
    int in, out, act; /* act: 0 identity, 1 relu */
    double *W;        /* out x in, row-major: y = W x + b */
    double *b;
} knc_layer;

typedef struct {
    int n_features, n_classes, n_layers, max_width;
    double *mean, *scale;
    knc_layer layers[KNC_MODEL_MAX_LAYERS];
} knc_model;

/* Returns 0 on success, nonzero on parse/shape error (message in err). */
int knc_model_load(knc_model *m, const char *path, char *err, size_t errlen);
void knc_model_free(knc_model *m);
/* Forward pass on one raw (unstandardized) feature vector.
 * scratch: 2*max_width doubles; logits: n_classes. Returns argmax
 * (first index on ties, as numpy.argmax). */
int knc_model_predict(const knc_model *m, const double *feat, double *scratch, double *logits);

#endif
