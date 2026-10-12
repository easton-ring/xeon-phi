"""Locate (and if needed build) the C binaries and shared library."""

from __future__ import annotations

import ctypes
import os
import shutil
import subprocess
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[2]


def build_dir() -> Path:
    return Path(os.environ.get("KNC_BUILD_DIR", REPO / "build"))


def ensure_built(quiet: bool = True) -> Path:
    """Configure and build the CMake project if the tree is available."""
    bd = build_dir()
    if not (REPO / "CMakeLists.txt").exists():
        raise FileNotFoundError("CMakeLists.txt not found; set KNC_BUILD_DIR to a prebuilt tree")
    if shutil.which("cmake") is None:
        raise FileNotFoundError("cmake not found")
    out = subprocess.DEVNULL if quiet else None
    if not (bd / "CMakeCache.txt").exists():
        subprocess.run(["cmake", "-S", str(REPO), "-B", str(bd), "-DCMAKE_BUILD_TYPE=Release"],
                       check=True, stdout=out)
    subprocess.run(["cmake", "--build", str(bd), "-j", str(os.cpu_count() or 2)], check=True, stdout=out)
    return bd


def find_binary(name: str) -> Path:
    p = build_dir() / name
    if not p.exists():
        ensure_built()
    if not p.exists():
        raise FileNotFoundError(p)
    return p


_F32P = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
_F64P = np.ctypeslib.ndpointer(dtype=np.float64, flags="C_CONTIGUOUS")


class KncPlan(ctypes.Structure):
    _fields_ = [("n", ctypes.c_size_t), ("log2n", ctypes.c_uint),
                ("bitrev", ctypes.c_void_p), ("tw64", ctypes.c_void_p), ("tw32", ctypes.c_void_p),
                ("hann64", ctypes.c_void_p), ("hann32", ctypes.c_void_p), ("hann_sumsq", ctypes.c_double)]


def load_lib() -> ctypes.CDLL:
    path = build_dir() / "libknc.so"
    if not path.exists():
        ensure_built()
    lib = ctypes.CDLL(str(path))
    sz = ctypes.c_size_t
    vp = ctypes.c_void_p
    for suf in ("f32", "f64"):
        for name, args in {
            "cmul": [vp, vp, vp, sz], "magsq": [vp, vp, sz], "mean_power": [vp, sz, sz, vp],
            "xcorr": [vp, sz, vp, sz, vp], "xcorr_batch": [vp, sz, sz, vp, sz, vp],
            "cfo": [vp, sz, sz, vp], "fft": [ctypes.POINTER(KncPlan), vp, ctypes.c_int],
            "fft_batch": [ctypes.POINTER(KncPlan), vp, sz],
            "psd": [ctypes.POINTER(KncPlan), vp, sz, ctypes.c_int, vp, vp],
        }.items():
            fn = getattr(lib, f"knc_{name}_{suf}")
            fn.argtypes = args
            fn.restype = ctypes.c_int
    lib.knc_fft_plan_init.argtypes = [ctypes.POINTER(KncPlan), sz]
    lib.knc_fft_plan_init.restype = ctypes.c_int
    lib.knc_fft_plan_free.argtypes = [ctypes.POINTER(KncPlan)]
    lib.knc_features_batch.argtypes = [vp, sz, sz, vp]
    lib.knc_features_batch.restype = ctypes.c_int
    return lib


def ptr(a: np.ndarray) -> ctypes.c_void_p:
    return ctypes.c_void_p(a.ctypes.data)


def c_features(lib: ctypes.CDLL, iq: np.ndarray) -> np.ndarray:
    """C features for complex64 (N, S) data."""
    from .features import NUM_FEATURES

    iq = np.ascontiguousarray(iq, dtype=np.complex64)
    out = np.empty((iq.shape[0], NUM_FEATURES), dtype=np.float64)
    rc = lib.knc_features_batch(ptr(iq), iq.shape[0], iq.shape[1], ptr(out))
    if rc != 0:
        raise RuntimeError(f"knc_features_batch returned {rc}")
    return out
