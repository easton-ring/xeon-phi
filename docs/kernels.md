# DSP kernels

Source: `csrc/include/knc/kernels.h`, `csrc/include/knc/fft.h`,
`csrc/src/kernels.c`, `csrc/src/fft.c`. All kernels are C99, take interleaved
complex arrays (`re0 im0 re1 im1 …`), exist in `float` (`_f32`) and `double`
(`_f64`) versions, and return `KNC_OK` (0) or `KNC_EINVAL` (1). `n` counts
complex samples. NumPy references: `python/knc_rf_lab/dsp_ref.py`.

## Build requirements that affect results

- **No `-ffast-math`.** It allows the compiler to assume no NaN/Inf, which
  breaks the propagation tests. The CMake build passes `-fno-fast-math`.
- **`-ffp-contract=off`.** Stops gcc from fusing `a*b+c` into FMA, so the
  float32 rounding is the same at every optimization level. Needed for the
  C-vs-NumPy feature agreement at 1e-9.
- **Intel `icc` defaults to `-fp-model fast=1`**, which is the same class of
  value-unsafe optimization. The KNC build (`mic/native/Makefile`) uses
  `-fp-model precise` (UNVERIFIED on hardware; see `docs/mpss-compatibility.md`).

## Definitions

| kernel | definition | invalid input |
|--------|------------|---------------|
| `knc_cmul` | `z[i] = x[i]·y[i]` | NULL with n > 0. n = 0 is a no-op. |
| `knc_magsq` | `out[i] = re² + im²` | NULL with n > 0 |
| `knc_mean_power` | `out[b] = (1/n) Σᵢ |x[b·n+i]|²`, accumulated in the input precision | n = 0, NULL |
| `knc_xcorr`, `knc_xcorr_batch` | `r[k] = Σ_{m=0}^{L-1} x[k+m]·conj(y[m])`, k = 0 … N−L (valid lags only; = `numpy.correlate(x, y, "valid")`) | L = 0, L > N, NULL |
| `knc_fft` | forward `X[k] = Σₙ x[n] e^{−j2πkn/N}`, unnormalized; inverse `x[n] = (1/N) Σₖ X[k] e^{+j2πkn/N}` (= `numpy.fft.fft`/`ifft`) | N not a power of two (rejected at plan creation) |
| `knc_psd` | `P[k] = |FFT(w·x)[k]|² / Σw²`; `w = 1` (rect) or periodic Hann `w[n] = ½ − ½cos(2πn/N)` | bad window id, Hann with N < 2 |
| `knc_cfo` | `f = arg(Σ_{i=0}^{n-2} x[i+1]·conj(x[i])) / 2π` cycles/sample, in (−½, ½] | n < 2 |

**Periodogram normalization.** Dividing by `Σw²` (`= N` for rect) makes the
expected value of every bin equal to the noise variance for white noise,
whatever the window. For the rect window, `Σₖ P[k] = Σₙ |x[n]|²` (Parseval).
This is `|FFT(w·x)|² / (N·Σw²/N)` from the brief. The Hann window is the
*periodic* form (scipy `get_window("hann", N)`), not `numpy.hanning`
(symmetric).

**FFT.** Iterative radix-2 decimation in time: bit-reversal permutation, then
log₂N butterfly stages. Twiddles `e^{−j2πk/N}`, k < N/2, are computed once per
plan in double precision (`cos`/`sin`) and rounded to float for the f32 path.
The inverse uses conjugated twiddles and then scales by 1/N.

## Error bounds used in tests

`ε` is the unit roundoff of the kernel precision (`FLT_EPSILON` or
`DBL_EPSILON`). The reference is float64 NumPy (pytest) or a double scalar
loop (CTest), computed on the same float inputs.

| kernel | test bound | reasoning |
|--------|-----------|-----------|
| cmul (each part) | `3ε·(|ac|+|bd|+|ad|+|bc|)` | two products and one sum, each rounded once |
| magsq | `2ε·|x|²` relative | two non-negative products and one sum |
| mean_power | `(2n+2)ε` relative | recursive sum of 2n non-negative terms (worst case `γ_{2n}`) |
| xcorr (each part) | `(L+2)ε·Σ(|a|+|b|)(|c|+|d|)` | recursive sum of L products, against the absolute-value correlation |
| cfo | `(2n+4)ε·Σ|x||x| / |r| / 2π` | angle error ≤ relative error of the sum; skipped when `|r|` ≈ 0 (ill-conditioned) |
| FFT | `‖X̂−X‖₂/‖X‖₂ ≤ 5·log₂N·ε` | normwise bound for radix-2 FFT grows as O(log₂N·ε) with accurate twiddles |
| inverse FFT round trip | `10·log₂N·ε` | two transforms |
| psd | `15·log₂N·ε` normwise | squared magnitude roughly doubles the FFT bound, plus window and scaling |

Measured errors are well under these bounds. The test sizes are 1, 2, 7, 8, 9,
15, 16, 17, 31, 32, 33, 63, 64, 65, 1024, 1025 complex samples for elementwise
kernels (around multiples of 8 complex = 16 floats = one 512-bit vector) and
1…4096 for the FFT. Each size runs at offsets of 0, 1 and 2 floats, so the base
pointer is misaligned by 4 or 8 bytes.

**NaN/Inf.** `magsq`, `mean_power`, `cmul`, `xcorr` and `cfo` propagate NaN;
`mean_power` and `magsq` propagate Inf. In the FFT a single NaN input
contaminates every output bin. An Inf input yields NaN in some bins, because
`Inf·0` inside the butterflies is NaN; the test only requires non-finite
output.

## OpenMP

Batched kernels parallelize over examples (`schedule(static)`). Elementwise
kernels parallelize over samples only when n > 65536. Loop indices are signed
`long` for compatibility with OpenMP 2.5/3.x runtimes.

## Frequency-offset estimator: bias on modulated signals

`E[x[i+1]·conj(x[i])] = |h|²·R_s(1)·e^{j2πf}`, where `R_s(1)` is the clean
signal's lag-1 autocorrelation. White noise adds nothing in expectation.
Three consequences:

1. The estimator is unbiased only if `R_s(1)` is real and positive. That holds
   for signals whose spectrum is symmetric about DC, which includes every
   built-in class. A signal with an asymmetric spectrum (SSB, an off-center
   OFDM allocation) has a bias of `arg R_s(1)/2π`.
2. Over a finite example the data symbols make the sum's phase random
   ("self-noise"). With FSK, an unequal number of +/− tones in the window
   biases the estimate by about `(n₊−n₋)/N · h/(2·sps)`. OFDM and FSK show the
   largest error at high SNR.
3. At low SNR the variance grows quickly, and the ±½ wrap makes the error
   heavy-tailed. The mean error is no longer meaningful there.

Measured on `data/dev/test.kncds` (dev config, |f| ≤ 0.01, N = 1024) with
`python scripts/cfo_bias.py data/dev/test.kncds`:

| class | SNR band (dB) | n | mean error | RMS error |
|---|---|---:|---:|---:|
| 16qam | [10, 20] | 58 | -0.0001 | 0.0030 |
| 16qam | [0, 10] | 74 | +0.0018 | 0.0162 |
| 16qam | [-10, 0] | 68 | +0.0031 | 0.0558 |
| 2fsk | [10, 20] | 75 | +0.0001 | 0.0045 |
| 2fsk | [0, 10] | 60 | -0.0026 | 0.0131 |
| 2fsk | [-10, 0] | 65 | -0.0095 | 0.0571 |
| 4fsk | [10, 20] | 61 | -0.0002 | 0.0131 |
| 4fsk | [0, 10] | 76 | -0.0019 | 0.0224 |
| 4fsk | [-10, 0] | 63 | +0.0008 | 0.0314 |
| 64qam | [10, 20] | 59 | -0.0001 | 0.0033 |
| 64qam | [0, 10] | 62 | +0.0007 | 0.0054 |
| 64qam | [-10, 0] | 79 | +0.0048 | 0.0445 |
| 8psk | [10, 20] | 62 | -0.0001 | 0.0025 |
| 8psk | [0, 10] | 68 | -0.0017 | 0.0198 |
| 8psk | [-10, 0] | 70 | -0.0020 | 0.0292 |
| bpsk | [10, 20] | 62 | +0.0002 | 0.0011 |
| bpsk | [0, 10] | 75 | -0.0008 | 0.0026 |
| bpsk | [-10, 0] | 63 | -0.0040 | 0.0581 |
| ofdm | [10, 20] | 60 | +0.0013 | 0.0151 |
| ofdm | [0, 10] | 62 | -0.0018 | 0.0291 |
| ofdm | [-10, 0] | 78 | -0.0262 | 0.1437 |
| qpsk | [10, 20] | 63 | -0.0003 | 0.0061 |
| qpsk | [0, 10] | 53 | +0.0007 | 0.0039 |
| qpsk | [-10, 0] | 84 | +0.0076 | 0.0428 |
