"""NumPy reference implementation of the classification features.

Must match csrc/src/features.c; formulas are documented in docs/features.md.
All computation is float64 from complex64 input.
"""

from __future__ import annotations

import numpy as np

FEATURE_NAMES = (
    "power_db", "power_var_norm", "amp_mean", "amp_m4", "sigma_aa",
    "c20_abs", "c40_abs", "c41_abs", "c42",
    "m2_spec_peak", "m4_spec_peak", "dphi_std", "dphi_kurt",
    "spec_centroid", "spec_spread", "spec_kurt", "spec_flatness",
)
NUM_FEATURES = len(FEATURE_NAMES)
TINY = 1e-300


def next_pow2(n: int) -> int:
    return 1 << (int(n) - 1).bit_length()


def features_one(x: np.ndarray) -> np.ndarray:
    x = np.asarray(x, dtype=np.complex64).astype(np.complex128)
    n = x.size
    if n < 2:
        raise ValueError("need at least 2 samples")
    nf = next_pow2(n)
    f = np.empty(NUM_FEATURES)
    p2 = x.real ** 2 + x.imag ** 2
    P = p2.mean()
    f[0] = 10 * np.log10(P + 1e-30)
    f[1] = np.mean((p2 - P) ** 2) / (P * P + TINY)

    xc = x - x.mean()
    pc = np.mean(xc.real ** 2 + xc.imag ** 2)
    y = xc * (1.0 / np.sqrt(pc + TINY))
    a2 = y.real ** 2 + y.imag ** 2
    a = np.sqrt(a2)
    am = a.mean()
    a4 = np.mean(a2 * a2)
    f[2] = am
    f[3] = a4
    f[4] = np.sqrt(np.mean((a / (am + TINY) - 1.0) ** 2))
    y2 = y * y
    m20 = y2.mean()
    m40 = np.mean(y2 * y2)
    m41 = np.mean(y2 * a2)
    c40 = m40 - 3 * m20 * m20
    c41 = m41 - 3 * m20  # M21 = 1
    f[5] = abs(m20)
    f[6] = abs(c40)
    f[7] = abs(c41)
    f[8] = a4 - abs(m20) ** 2 - 2.0
    f[9] = np.abs(np.fft.fft(y2, nf)).max() / n
    f[10] = np.abs(np.fft.fft(y2 * y2, nf)).max() / n

    r = x[1:] * np.conj(x[:-1])
    d = np.arctan2(r.imag, r.real)
    dc = d - d.mean()
    d2 = np.mean(dc ** 2)
    d4 = np.mean(dc ** 4)
    f[11] = np.sqrt(d2)
    f[12] = d4 / (d2 * d2) if d2 > 0 else 0.0

    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)
    pw = np.abs(np.fft.fft(w * x, nf)) ** 2
    tot = pw.sum()
    fr = np.fft.fftfreq(nf)  # k/nf for k < nf/2, k/nf - 1 otherwise
    cen = np.sum(fr * pw) / (tot + TINY)
    t = (fr - cen) ** 2
    spr = np.sum(t * pw) / (tot + TINY)
    sk = np.sum(t * t * pw) / (tot + TINY)
    f[13] = cen
    f[14] = np.sqrt(spr)
    f[15] = sk / (spr * spr) if spr > 0 else 0.0
    mean_p = tot / nf
    f[16] = np.exp(np.mean(np.log(pw + 1e-12 * mean_p + TINY))) / (mean_p + TINY)
    return f


def features_batch(iq: np.ndarray) -> np.ndarray:
    iq = np.asarray(iq)
    return np.stack([features_one(row) for row in iq]) if len(iq) else np.zeros((0, NUM_FEATURES))
