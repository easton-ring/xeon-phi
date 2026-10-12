"""Cross-language checks: C kernels/features/reader vs NumPy and the Python reader."""

import ctypes
import json
import subprocess

import numpy as np
import pytest

from corruptions import CASES, make_case
from knc_rf_lab import dsp_ref
from knc_rf_lab.cbin import KncPlan, c_features, find_binary, load_lib, ptr
from knc_rf_lab.dsformat import Dataset
from knc_rf_lab.features import features_batch

EPS = {np.float32: np.finfo(np.float32).eps, np.float64: np.finfo(np.float64).eps}
SUF = {np.float32: "f32", np.float64: "f64"}
CPLX = {np.float32: np.complex64, np.float64: np.complex128}


@pytest.fixture(scope="module")
def lib():
    return load_lib()


def rand_c(rng, shape, dt):
    return (rng.standard_normal(shape) + 1j * rng.standard_normal(shape)).astype(CPLX[dt])


@pytest.mark.parametrize("dt", [np.float32, np.float64])
@pytest.mark.parametrize("n", [1, 7, 8, 9, 16, 17, 64, 1000])
def test_elementwise_vs_numpy(lib, dt, n):
    rng = np.random.default_rng(n)
    x, y = rand_c(rng, n, dt), rand_c(rng, n, dt)
    z = np.empty_like(x)
    assert getattr(lib, f"knc_cmul_{SUF[dt]}")(ptr(x), ptr(y), ptr(z), n) == 0
    ref = dsp_ref.cmul(x, y)
    mag = np.abs(x.real * y.real) + np.abs(x.imag * y.imag) + np.abs(x.real * y.imag) + np.abs(x.imag * y.real)
    assert np.all(np.abs(z.real - ref.real) <= 3 * EPS[dt] * mag)
    assert np.all(np.abs(z.imag - ref.imag) <= 3 * EPS[dt] * mag)
    m = np.empty(n, dtype=dt)
    assert getattr(lib, f"knc_magsq_{SUF[dt]}")(ptr(x), ptr(m), n) == 0
    np.testing.assert_allclose(m, dsp_ref.magsq(x), rtol=2 * EPS[dt], atol=0)


@pytest.mark.parametrize("dt", [np.float32, np.float64])
def test_mean_power_and_cfo_vs_numpy(lib, dt):
    rng = np.random.default_rng(1)
    b, n = 5, 1024
    f0 = np.array([-0.2, -0.01, 0.0, 0.003, 0.3])
    x = (np.exp(2j * np.pi * f0[:, None] * np.arange(n)) + 0.1 * rand_c(rng, (b, n), np.float64)).astype(CPLX[dt])
    out = np.empty(b, dtype=dt)
    assert getattr(lib, f"knc_mean_power_{SUF[dt]}")(ptr(x), b, n, ptr(out)) == 0
    np.testing.assert_allclose(out, dsp_ref.mean_power(x), rtol=(2 * n + 2) * EPS[dt])
    assert getattr(lib, f"knc_cfo_{SUF[dt]}")(ptr(x), b, n, ptr(out)) == 0
    ref = dsp_ref.cfo(x)
    np.testing.assert_allclose(out, ref, atol=(2 * n + 4) * EPS[dt])
    np.testing.assert_allclose(ref, f0, atol=2e-3)  # estimator itself, at 20 dB SNR


@pytest.mark.parametrize("dt", [np.float32, np.float64])
@pytest.mark.parametrize("n,l", [(16, 16), (100, 7), (1024, 64)])
def test_xcorr_vs_numpy(lib, dt, n, l):
    rng = np.random.default_rng(n + l)
    x, y = rand_c(rng, (3, n), dt), rand_c(rng, l, dt)
    r = np.empty((3, n - l + 1), dtype=CPLX[dt])
    assert getattr(lib, f"knc_xcorr_batch_{SUF[dt]}")(ptr(x), 3, n, ptr(y), l, ptr(r)) == 0
    for b in range(3):
        ref = dsp_ref.xcorr(x[b], y)
        bound = (l + 2) * EPS[dt] * np.correlate(np.abs(x[b].real) + np.abs(x[b].imag),
                                                 np.abs(y.real) + np.abs(y.imag), "valid")
        assert np.all(np.abs(r[b].real - ref.real) <= bound)
        assert np.all(np.abs(r[b].imag - ref.imag) <= bound)


@pytest.mark.parametrize("dt", [np.float32, np.float64])
@pytest.mark.parametrize("n", [1, 2, 8, 64, 1024, 4096])
def test_fft_psd_vs_numpy(lib, dt, n):
    plan = KncPlan()
    assert lib.knc_fft_plan_init(ctypes.byref(plan), n) == 0
    try:
        rng = np.random.default_rng(n)
        x = rand_c(rng, (4, n), dt)
        lg = max(1, int(np.log2(n)))
        X = x.copy()
        assert getattr(lib, f"knc_fft_batch_{SUF[dt]}")(ctypes.byref(plan), ptr(X), 4) == 0
        ref = np.fft.fft(x.astype(np.complex128), axis=-1)
        err = np.linalg.norm(X - ref, axis=-1) / np.linalg.norm(ref, axis=-1)
        assert np.all(err <= 5 * lg * EPS[dt])
        xi = X.copy()
        for b in range(4):
            assert getattr(lib, f"knc_fft_{SUF[dt]}")(ctypes.byref(plan), ptr(xi[b]), 1) == 0
        err = np.linalg.norm(xi - np.fft.ifft(X.astype(np.complex128), axis=-1), axis=-1) / np.linalg.norm(x, axis=-1)
        assert np.all(err <= 10 * lg * EPS[dt])
        for wi, wname in ((0, "rect"), (1, "hann")):
            work = np.empty_like(x)
            P = np.empty((4, n), dtype=dt)
            rc = getattr(lib, f"knc_psd_{SUF[dt]}")(ctypes.byref(plan), ptr(x), 4, wi, ptr(work), ptr(P))
            if n == 1 and wname == "hann":
                assert rc == 1  # Hann needs n >= 2
            else:
                assert rc == 0
                ref = dsp_ref.psd(x, wname)
                err = np.linalg.norm(P - ref, axis=-1) / np.linalg.norm(ref, axis=-1)
                assert np.all(err <= 15 * lg * EPS[dt])
    finally:
        lib.knc_fft_plan_free(ctypes.byref(plan))


def test_features_c_vs_numpy(lib, tiny_dataset):
    ds = Dataset(tiny_dataset / "train.kncds")
    iq = np.asarray(ds.iq)
    fc = c_features(lib, iq)
    fp = features_batch(iq)
    np.testing.assert_allclose(fc, fp, rtol=1e-9, atol=1e-9)
    # non power-of-two length (zero-padded FFTs)
    fc = c_features(lib, iq[:, :200])
    fp = features_batch(iq[:, :200])
    np.testing.assert_allclose(fc, fp, rtol=1e-9, atol=1e-9)


def test_c_validator_ok(tiny_dataset):
    exe = find_binary("knc_validate")
    p = subprocess.run([str(exe), str(tiny_dataset / "train.kncds")], capture_output=True, text=True)
    assert p.returncode == 0, p.stdout + p.stderr
    r = json.loads(p.stdout)
    meta = json.loads((tiny_dataset / "train.json").read_text())
    assert r["status"] == "ok" and r["sha256"] == meta["sha256"] and r["num_examples"] == 48


@pytest.mark.parametrize("case", sorted(CASES))
def test_c_validator_corruption(tiny_dataset, tmp_path, case):
    bad = tmp_path / f"{case}.kncds"
    code = make_case(tiny_dataset / "train.kncds", bad, case)
    p = subprocess.run([str(find_binary("knc_validate")), str(bad)], capture_output=True, text=True)
    assert p.returncode == 1
    assert json.loads(p.stdout)["code"] == code
