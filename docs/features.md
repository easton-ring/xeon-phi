# Classification features and model export

The features are implemented twice and must agree:

- NumPy: `python/knc_rf_lab/features.py`
- C99: `csrc/src/features.c`

Both compute in float64 from complex64 input. `tests/test_c_core.py::test_features_c_vs_numpy`
requires agreement within `rtol = atol = 1e-9`, at N = 256 and at a non-power-of-two
length. Remaining differences come from summation order and from radix-2 vs
pocketfft rounding.

## Notation

`x[n]`, n = 0 … N−1: one example (complex). `N_f`: the smallest power of two
≥ N. FFTs are zero-padded to `N_f`. `mean(·)` averages over the N samples
(N−1 for phase differences). `tiny = 1e-300` guards divisions.

- `P = mean|x|²`
- `x_c = x − mean(x)`
- `y = x_c / sqrt(mean|x_c|²)`: zero-mean, unit power
- `a = |y|`
- Moments: `M_pq = mean(y^{p−q} · conj(y)^q)`. By construction `M21 = 1`.

## Features (17, in order)

| # | name | definition | captures |
|---|------|------------|----------|
| 0 | `power_db` | `10·log10(P + 1e-30)` | received power (fading × SNR) |
| 1 | `power_var_norm` | `mean((|x|² − P)²) / P²` | envelope fluctuation, including noise |
| 2 | `amp_mean` | `mean(a)` | normalized amplitude moment 1 |
| 3 | `amp_m4` | `mean(a⁴)` (= M42) | normalized amplitude moment 4 |
| 4 | `sigma_aa` | `sqrt(mean((a/mean(a) − 1)²))` | std of normalized centered amplitude (constant envelope → 0) |
| 5 | `c20_abs` | `|C20|`, `C20 = M20` | BPSK / real-valued constellations |
| 6 | `c40_abs` | `|C40|`, `C40 = M40 − 3·M20²` | PSK order, QAM |
| 7 | `c41_abs` | `|C41|`, `C41 = M41 − 3·M20·M21` | BPSK vs others |
| 8 | `c42` | `C42 = M42 − |M20|² − 2·M21²` (real) | QAM vs PSK (symbol-spaced: BPSK −2, QPSK/8PSK −1, 16-QAM −0.68, 64-QAM −0.62) |
| 9 | `m2_spec_peak` | `max_k |FFT_{N_f}(y²)[k]| / N` | |M20| robust to frequency offset |
| 10 | `m4_spec_peak` | `max_k |FFT_{N_f}(y⁴)[k]| / N` | |M40| robust to frequency offset (BPSK, QPSK and square QAM have a 4th-power line; 8PSK does not) |
| 11 | `dphi_std` | `std(d)`, `d[n] = arg(x[n+1]·conj(x[n]))` | instantaneous frequency spread |
| 12 | `dphi_kurt` | `mean((d−d̄)⁴) / mean((d−d̄)²)²` (0 if the variance is 0) | FSK (bimodal or multimodal) vs PSK |
| 13 | `spec_centroid` | `Σ f_k·p_k`, `p_k = P_k / ΣP` | frequency offset |
| 14 | `spec_spread` | `sqrt(Σ (f_k − μ)²·p_k)` | occupied bandwidth (sps, roll-off) |
| 15 | `spec_kurt` | `Σ (f_k − μ)⁴·p_k / spread⁴` | spectral shape (OFDM is flat-topped) |
| 16 | `spec_flatness` | `exp(mean log(P_k + 1e-12·P̄)) / P̄`, `P̄ = mean P_k` | geometric/arithmetic mean ratio |

Spectral features use `P_k = |FFT_{N_f}(w·x)[k]|²` with the periodic Hann
window `w[n] = ½ − ½cos(2πn/N)` over the N real samples (then zero-padded),
and `f_k = k/N_f` for `k < N_f/2`, else `k/N_f − 1` (`numpy.fft.fftfreq`).

Cumulants follow the standard zero-mean definitions (Swami & Sadler, "Hierarchical
digital modulation classification using cumulants", IEEE Trans. Commun. 48(3), 2000).
The values above are for symbol-spaced constellations. With RRC pulse shaping
at `sps > 1` the measured cumulants shrink toward the Gaussian value of 0;
the classifier learns from that.

Why the frequency-offset-robust peaks (9, 10): with `|f| ≤ 0.01` and N = 1024,
`y⁴` rotates by up to 4·0.01·1024 ≈ 41 cycles over the example, so a plain
`|M40|` averages to about 0. The FFT peak of `y⁴` finds the rotated line.

## Models

`python -m knc_rf_lab.classify train` fits two scikit-learn pipelines on the
train split. Each is `StandardScaler` → classifier:

- `logreg`: `LogisticRegression(max_iter=5000, C=1.0)` (multinomial)
- `mlp`: `MLPClassifier(hidden_layer_sizes=(64, 32), activation="relu",
  alpha=1e-3, max_iter=2000, random_state=0)`

No random forests: they do not export to plain arrays (brief §8).

## Model export

`<model>.txt` (read by `csrc/src/model.c`) is whitespace-separated text with
`%.17g` doubles, so the parse round-trips exactly:

```
KNCMODEL 1
<n_features> <n_classes> <n_layers>
<mean[0..F-1]>                 StandardScaler.mean_
<scale[0..F-1]>                StandardScaler.scale_
repeat n_layers times:
  <in> <out> <act>             act: 0 identity, 1 relu
  <out rows of in values>      W, row-major, y = W·a + b
  <b[0..out-1]>
```

- Logistic regression: one identity layer, `W = coef_`, `b = intercept_`.
- MLP: `W = coefs_[i].T`, ReLU on hidden layers, identity on the output
  (sklearn's softmax is monotone, so argmax is unchanged).
- Prediction is `argmax(logits)`, first index on ties (same as `numpy.argmax`).
  The model must have seen all classes, so `classes_ == 0 … C−1`.

`<model>.json` holds the same arrays for humans. `<model>.pkl` is the
sklearn pipeline, used for evaluation only.

## C inference check

`python -m knc_rf_lab.classify infer_check` runs `knc_infer`, which computes
**C features** and the **C forward pass** on `test.kncds`, then compares with
`sklearn.predict` on **NumPy features**. Labels must be identical, except where
the top-two logit margin is below `1e-6` (a documented tie-break). Those are
listed as explained mismatches; any other mismatch fails the check.

Dev config result (`results/eval/infer_check.json`): 0 mismatches out of 1600
for both models; max |logit difference| 2.6e-13 (logreg) and 1.8e-12 (mlp).
