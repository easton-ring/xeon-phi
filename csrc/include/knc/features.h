#ifndef KNC_FEATURES_H
#define KNC_FEATURES_H
/* Hand-crafted modulation features (docs/features.md). Computed in double
 * precision from float32 I/Q; the NumPy reference is knc_rf_lab.features. */
#include <stddef.h>

#include "knc/fft.h"

#define KNC_NUM_FEATURES 17

const char *knc_feature_name(int i);

/* Features of one example of n >= 2 samples. plan->n must be the smallest
 * power of two >= n. work must hold 6*plan->n doubles. */
int knc_features(const knc_fft_plan *plan, const float *x, size_t n, double *work, double *feat);

/* Batched (OpenMP over examples); feat is batch*KNC_NUM_FEATURES. Allocates
 * per-thread scratch internally. Returns KNC_EINVAL or 2 on allocation failure. */
int knc_features_batch(const float *x, size_t batch, size_t n, double *feat);

#endif
