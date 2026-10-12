"""NumPy float64 references for the C kernels (definitions in docs/kernels.md)."""

from __future__ import annotations

import numpy as np


def cmul(x, y):
    return np.asarray(x, np.complex128) * np.asarray(y, np.complex128)


def magsq(x):
    x = np.asarray(x, np.complex128)
    return x.real ** 2 + x.imag ** 2


def mean_power(x):
    """x: (batch, n) complex -> (batch,)"""
    return magsq(x).mean(axis=-1)


def xcorr(x, y):
    """numpy.correlate(x, y, 'valid'): r[k] = sum_m x[k+m] conj(y[m])"""
    return np.correlate(np.asarray(x, np.complex128), np.asarray(y, np.complex128), "valid")


def hann(n: int) -> np.ndarray:
    """Periodic Hann window."""
    return 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(n) / n)


def psd(x, window: str = "rect"):
    """Periodogram |FFT(w x)|^2 / sum(w^2) along the last axis."""
    x = np.asarray(x, np.complex128)
    n = x.shape[-1]
    w = hann(n) if window == "hann" else np.ones(n)
    return np.abs(np.fft.fft(x * w, axis=-1)) ** 2 / np.sum(w * w)


def cfo(x):
    """arg(sum x[i+1] conj(x[i])) / 2pi along the last axis (cycles/sample)."""
    x = np.asarray(x, np.complex128)
    return np.angle(np.sum(x[..., 1:] * np.conj(x[..., :-1]), axis=-1)) / (2 * np.pi)
