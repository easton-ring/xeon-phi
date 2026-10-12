"""Built-in deterministic RF modulation dataset generator (NumPy only).

Every example draws from its own ``numpy.random.Generator(PCG64)`` whose
SeedSequence is spawned from a per-split SeedSequence:

    split_ss   = SeedSequence(entropy=master_seed, spawn_key=(crc32(split),))
    children   = split_ss.spawn(1 + N)
    children[0]      -> class-order permutation for the split
    children[1 + i]  -> everything about example i

Signal model for one example (see docs/dataset-format.md, "Signal model"):

    x[n] = h * s[n] * exp(j(2*pi*f*n + phi)) + w[n]

where ``s`` is the clean modulated signal scaled to unit mean power, ``h`` the
fading gain (E|h|^2 = 1 for Rayleigh), ``f`` the frequency offset in
cycles/sample, ``phi`` the phase offset and ``w`` complex AWGN with variance
10^(-snr_db/10). ``snr_db`` is therefore the average (pre-fading) SNR.
"""

from __future__ import annotations

import csv
import io
import json
import platform
import zlib
from pathlib import Path

import numpy as np

from . import __version__
from .config import load_config
from .dsformat import FIELD_OFFSETS, write_dataset

GENERATOR_NAME = "knc_rf_lab.builtin"
GENERATOR_VERSION = "1.0"
SPLITS = ("train", "val", "test")

IMPAIRMENT_COLUMNS = (
    "index", "class_id", "class_name", "snr_db", "freq_offset", "phase_offset",
    "fading", "fading_gain_re", "fading_gain_im", "sps", "rrc_beta",
    "timing_offset", "start_offset", "mod_index", "ofdm_qam_order",
)


# ---------------------------------------------------------------- pulses

def rrc_taps(beta: float, sps: int, span: int, tau: float = 0.0) -> np.ndarray:
    """Root-raised-cosine taps h((m - tau)/sps), m = -span*sps/2 .. span*sps/2.

    ``tau`` is a fractional delay in samples; unit-energy normalized.
    """
    half = span * sps // 2
    t = (np.arange(-half, half + 1, dtype=np.float64) - tau) / sps
    h = np.empty_like(t)
    eps = 1e-9
    at0 = np.abs(t) < eps
    sing = np.abs(np.abs(t) - 1.0 / (4.0 * beta)) < eps if beta > 0 else np.zeros_like(at0)
    reg = ~(at0 | sing)
    tr = t[reg]
    num = np.sin(np.pi * tr * (1 - beta)) + 4 * beta * tr * np.cos(np.pi * tr * (1 + beta))
    den = np.pi * tr * (1 - (4 * beta * tr) ** 2)
    h[reg] = num / den
    h[at0] = 1 - beta + 4 * beta / np.pi
    if beta > 0:
        h[sing] = beta / np.sqrt(2) * ((1 + 2 / np.pi) * np.sin(np.pi / (4 * beta))
                                       + (1 - 2 / np.pi) * np.cos(np.pi / (4 * beta)))
    return h / np.sqrt(np.sum(h * h))


# ---------------------------------------------------------- constellations

def psk(m: int) -> np.ndarray:
    off = np.pi / 4 if m == 4 else 0.0
    return np.exp(1j * (2 * np.pi * np.arange(m) / m + off))


def qam(m: int) -> np.ndarray:
    k = int(round(np.sqrt(m)))
    lv = np.arange(-(k - 1), k, 2, dtype=np.float64)
    pts = (lv[:, None] + 1j * lv[None, :]).ravel()
    return pts / np.sqrt(2.0 * (m - 1) / 3.0)


CONSTELLATIONS = {"bpsk": psk(2), "qpsk": psk(4), "8psk": psk(8), "16qam": qam(16), "64qam": qam(64)}
FSK_ORDER = {"2fsk": 2, "4fsk": 4}


# ---------------------------------------------------------- modulators

def _linear(rng, cls, n, cfg, p):
    lin = cfg["linear"]
    sps = int(rng.choice(lin["sps"]))
    beta = float(rng.uniform(*lin["rrc_beta"]))
    tau = float(rng.uniform(*cfg["impairments"]["timing_offset"]))
    span = int(lin["span_symbols"])
    nsym = -(-n // sps) + span + 2
    const = CONSTELLATIONS[cls]
    sym = const[rng.integers(0, len(const), nsym)]
    up = np.zeros(nsym * sps, dtype=np.complex128)
    up[::sps] = sym
    y = np.convolve(up, rrc_taps(beta, sps, span, tau))
    start = span * sps  # skip the filter transient
    p.update(sps=sps, rrc_beta=beta, timing_offset=tau, start_offset=start)
    return y[start:start + n]


def _fsk(rng, cls, n, cfg, p):
    m = FSK_ORDER[cls]
    sps = int(rng.choice(cfg["fsk"]["sps"]))
    h = float(rng.uniform(*cfg["fsk"]["mod_index"]))
    start = int(rng.integers(0, sps))
    nsym = -(-(n + start) // sps)
    d = 2 * rng.integers(0, m, nsym) - (m - 1)
    dphi = np.repeat(np.pi * h * d / sps, sps)
    phase = np.cumsum(dphi)
    p.update(sps=sps, mod_index=h, start_offset=start)
    return np.exp(1j * phase[start:start + n])


def _ofdm(rng, cls, n, cfg, p):
    o = cfg["ofdm"]
    nfft, cp, act = int(o["nfft"]), int(o["cp_len"]), int(o["active_subcarriers"])
    order = int(rng.choice(o["qam_orders"]))
    const = qam(order)
    half = act // 2
    bins = np.r_[1:half + 1, nfft - half:nfft]  # skip DC
    symlen = nfft + cp
    start = int(rng.integers(0, symlen))
    nsym = -(-(n + start) // symlen)
    grid = np.zeros((nsym, nfft), dtype=np.complex128)
    grid[:, bins] = const[rng.integers(0, len(const), (nsym, len(bins)))]
    td = np.fft.ifft(grid, axis=1)
    td = np.concatenate([td[:, -cp:], td], axis=1).ravel()
    p.update(start_offset=start, ofdm_qam_order=order)
    return td[start:start + n]


def _modulate(rng, cls, n, cfg, p):
    if cls in CONSTELLATIONS:
        return _linear(rng, cls, n, cfg, p)
    if cls in FSK_ORDER:
        return _fsk(rng, cls, n, cfg, p)
    if cls == "ofdm":
        return _ofdm(rng, cls, n, cfg, p)
    raise ValueError(cls)


def generate_example(rng: np.random.Generator, cls: str, cfg: dict) -> tuple[np.ndarray, dict]:
    """Return (complex128 signal of length S, impairment parameters)."""
    n = cfg["samples_per_example"]
    imp = cfg["impairments"]
    p = {k: float("nan") for k in IMPAIRMENT_COLUMNS[3:]}
    p["snr_db"] = float(rng.uniform(*cfg["snr_db"]))
    p["freq_offset"] = float(rng.uniform(*imp["freq_offset"]))
    p["phase_offset"] = float(rng.uniform(*imp["phase_offset"]))
    kinds = sorted(imp["fading"])
    w = np.array([imp["fading"][k] for k in kinds], dtype=np.float64)
    kind = kinds[int(rng.choice(len(kinds), p=w / w.sum()))]
    if kind == "none":
        g = 1.0 + 0j
    elif kind == "flat":
        g = complex(10 ** (rng.uniform(*imp["flat_gain_db"]) / 20))
    else:
        g = complex(rng.standard_normal(), rng.standard_normal()) / np.sqrt(2)
    p.update(fading=kind, fading_gain_re=g.real, fading_gain_im=g.imag)

    s = _modulate(rng, cls, n, cfg, p)
    s = s / np.sqrt(np.mean(np.abs(s) ** 2))
    t = np.arange(n, dtype=np.float64)
    x = g * s * np.exp(1j * (2 * np.pi * p["freq_offset"] * t + p["phase_offset"]))
    sigma = np.sqrt(10 ** (-p["snr_db"] / 10) / 2)
    x = x + sigma * (rng.standard_normal(n) + 1j * rng.standard_normal(n))
    return x, p


def split_seed_sequence(master_seed: int, split: str) -> np.random.SeedSequence:
    return np.random.SeedSequence(entropy=int(master_seed), spawn_key=(zlib.crc32(split.encode()),))


def generate_split(cfg: dict, split: str):
    """Generate one split. Returns (iq complex64 (N,S), class_ids, snr_db, params list)."""
    classes = cfg["classes"]
    per = cfg["examples_per_class"][split]
    total = per * len(classes)
    ss = split_seed_sequence(cfg["master_seed"], split)
    children = ss.spawn(1 + total)
    order = np.repeat(np.arange(len(classes)), per)
    order = np.random.Generator(np.random.PCG64(children[0])).permutation(order)
    iq = np.empty((total, cfg["samples_per_example"]), dtype=np.complex64)
    params = []
    for i in range(total):
        rng = np.random.Generator(np.random.PCG64(children[1 + i]))
        cid = int(order[i])
        x, p = generate_example(rng, classes[cid], cfg)
        iq[i] = x
        p.update(index=i, class_id=cid, class_name=classes[cid])
        params.append(p)
    snr = np.array([p["snr_db"] for p in params], dtype=np.float32)
    return iq, order.astype(np.int32), snr, params


def _params_csv(params: list[dict]) -> bytes:
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(IMPAIRMENT_COLUMNS)
    for p in params:
        w.writerow([repr(p[c]) if isinstance(p[c], float) else p[c] for c in IMPAIRMENT_COLUMNS])
    return buf.getvalue().encode()


def write_split(cfg: dict, split: str, out_dir: Path) -> dict:
    import hashlib

    iq, cid, snr, params = generate_split(cfg, split)
    out_dir.mkdir(parents=True, exist_ok=True)
    ds_path = out_dir / f"{split}.kncds"
    sha = write_dataset(ds_path, iq, cid, snr, len(cfg["classes"]))
    csv_bytes = _params_csv(params)
    csv_path = out_dir / f"{split}.impairments.csv"
    csv_path.write_bytes(csv_bytes)
    ss = split_seed_sequence(cfg["master_seed"], split)
    sidecar = {
        "format": "KNCRFDS",
        "format_version": "1.0",
        "split": split,
        "file": ds_path.name,
        "sha256": sha,
        "num_examples": int(iq.shape[0]),
        "samples_per_example": int(iq.shape[1]),
        "sample_format": "complex64_interleaved_iq_le",
        "sample_rate_hz": cfg["sample_rate_hz"],
        "class_map": {c: i for i, c in enumerate(cfg["classes"])},
        "generator": {"name": GENERATOR_NAME, "version": GENERATOR_VERSION},
        "config": cfg,
        "seeds": {
            "master_seed": cfg["master_seed"],
            "split_spawn_key": list(ss.spawn_key),
            "bit_generator": "PCG64",
            "derivation": "SeedSequence(entropy=master_seed, spawn_key=(crc32(split),)).spawn(1+N)",
        },
        "software": {
            "knc_rf_lab": __version__,
            "numpy": np.__version__,
            "python": platform.python_version(),
        },
        "impairments": {"file": csv_path.name, "sha256": hashlib.sha256(csv_bytes).hexdigest(),
                        "columns": list(IMPAIRMENT_COLUMNS)},
        "header_field_offsets": FIELD_OFFSETS,
    }
    (out_dir / f"{split}.json").write_text(json.dumps(sidecar, indent=2, sort_keys=True) + "\n")
    return sidecar


def generate(cfg_path: str | Path | None, out_dir: str | Path, overrides: dict | None = None) -> list[dict]:
    cfg = load_config(cfg_path, overrides)
    if cfg["generator"] != "builtin":
        from . import torchsig_adapter
        return torchsig_adapter.generate(cfg, Path(out_dir))
    return [write_split(cfg, split, Path(out_dir))
            for split in SPLITS if cfg["examples_per_class"].get(split, 0) > 0]
