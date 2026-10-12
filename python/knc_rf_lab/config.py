"""Dataset generation config: YAML loading, defaults and validation."""

from __future__ import annotations

import copy
from pathlib import Path

import yaml

CLASS_NAMES = ("bpsk", "qpsk", "8psk", "16qam", "64qam", "2fsk", "4fsk", "ofdm")

DEFAULTS = {
    "name": "dev",
    "generator": "builtin",
    "master_seed": 20261012,
    "samples_per_example": 1024,
    "sample_rate_hz": 1.0e6,  # nominal; all impairments are in normalized units
    "classes": list(CLASS_NAMES),
    "examples_per_class": {"train": 200, "val": 200, "test": 200},
    "snr_db": [-10.0, 20.0],
    "impairments": {
        "freq_offset": [-0.01, 0.01],  # cycles/sample
        "phase_offset": [0.0, 6.283185307179586],  # rad
        "timing_offset": [0.0, 1.0],  # fraction of a sample
        "fading": {"none": 0.5, "flat": 0.25, "rayleigh": 0.25},
        "flat_gain_db": [-3.0, 3.0],
    },
    "linear": {"sps": [4, 8], "rrc_beta": [0.2, 0.5], "span_symbols": 8},
    "fsk": {"sps": [8], "mod_index": [0.5, 1.0]},
    "ofdm": {"nfft": 64, "cp_len": 16, "active_subcarriers": 52, "qam_orders": [4, 16]},
}


def _merge(base: dict, over: dict) -> dict:
    out = copy.deepcopy(base)
    for k, v in over.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict) and k != "examples_per_class":
            out[k] = _merge(out[k], v)
        else:
            out[k] = copy.deepcopy(v)
    return out


def _range(cfg: dict, key: str, path: str) -> None:
    v = cfg[key]
    if not (isinstance(v, (list, tuple)) and len(v) == 2 and float(v[0]) <= float(v[1])):
        raise ValueError(f"{path}{key} must be [lo, hi] with lo <= hi, got {v!r}")
    cfg[key] = [float(v[0]), float(v[1])]


def validate(cfg: dict) -> dict:
    unknown = set(cfg) - set(DEFAULTS)
    if unknown:
        raise ValueError(f"unknown config keys: {sorted(unknown)}")
    for c in cfg["classes"]:
        if c not in CLASS_NAMES:
            raise ValueError(f"unknown class {c!r}; built-in classes are {CLASS_NAMES}")
    if len(set(cfg["classes"])) != len(cfg["classes"]):
        raise ValueError("duplicate class names")
    s = int(cfg["samples_per_example"])
    if not 16 <= s <= 65536:
        raise ValueError("samples_per_example must be in [16, 65536]")
    cfg["samples_per_example"] = s
    for split, n in cfg["examples_per_class"].items():
        if not isinstance(n, int) or n < 0:
            raise ValueError(f"examples_per_class[{split}] must be a non-negative int")
    _range(cfg, "snr_db", "")
    imp = cfg["impairments"]
    for k in ("freq_offset", "phase_offset", "timing_offset", "flat_gain_db"):
        _range(imp, k, "impairments.")
    fad = imp["fading"]
    if set(fad) - {"none", "flat", "rayleigh"} or sum(fad.values()) <= 0:
        raise ValueError("impairments.fading must map none/flat/rayleigh to weights")
    _range(cfg["linear"], "rrc_beta", "linear.")
    _range(cfg["fsk"], "mod_index", "fsk.")
    return cfg


def load_config(path: str | Path | None = None, overrides: dict | None = None) -> dict:
    cfg = copy.deepcopy(DEFAULTS)
    if path is not None:
        with open(path) as f:
            user = yaml.safe_load(f) or {}
        cfg = _merge(cfg, user)
    if overrides:
        cfg = _merge(cfg, overrides)
    return validate(cfg)
