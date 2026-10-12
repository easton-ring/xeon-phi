"""TorchSig 2.2.0 adapter: the only module that imports torchsig.

Status: verified in this container against torchsig 2.2.0 / torch 2.14.1
(CUDA wheel, run on CPU) in a separate venv; see docs/torchsig.md. Other
TorchSig versions are UNVERIFIED — the API changed substantially between 0.x,
1.x and 2.x. Enable with ``generator: torchsig`` in the YAML config.

API used (paths relative to site-packages/torchsig/):
  datasets/datasets.py   TorchSigIterableDataset(signal_generators=[...], metadata=..., target_labels=None, seed=...)
  utils/defaults.py      TorchSigDefaults().default_dataset_metadata
  signals/signal_lists.py  real class names ("bpsk", "ofdm-64", ...)
Each ``next(ds)`` returns a Signal; ``.data`` is complex64 (num_iq_samples,),
``.component_signals[0]`` holds class_name / snr_db / bandwidth metadata.

What the built-in generator's impairments map to: only the SNR range is passed
through. TorchSig signals are generated at center frequency 0 without
TorchSig's impairment transforms; our freq/phase/fading ranges are ignored.
"""

from __future__ import annotations

import csv
import hashlib
import io
import json
import os
import platform
import zlib
from pathlib import Path

import numpy as np

from . import __version__
from .dsformat import write_dataset

# our class name -> TorchSig 2.2.0 class name (signals/signal_lists.py)
CLASS_MAP = {"bpsk": "bpsk", "qpsk": "qpsk", "8psk": "8psk", "16qam": "16qam",
             "64qam": "64qam", "2fsk": "2fsk", "4fsk": "4fsk", "ofdm": "ofdm-64"}
SUPPORTED_VERSION = "2.2.0"
COLUMNS = ("index", "class_id", "class_name", "torchsig_class", "snr_db", "bandwidth_hz",
           "center_freq_hz", "sample_rate_hz", "seed")


def _import():
    os.environ.setdefault("CUDA_VISIBLE_DEVICES", "")
    try:
        import torchsig
        from torchsig.datasets.datasets import TorchSigIterableDataset
        from torchsig.utils.defaults import TorchSigDefaults
    except ImportError as e:  # pragma: no cover - depends on environment
        raise RuntimeError(f"torchsig is not installed ({e}); pip install '.[torchsig]'") from e
    if torchsig.__version__ != SUPPORTED_VERSION:
        raise RuntimeError(f"torchsig {torchsig.__version__} is UNVERIFIED; adapter written for {SUPPORTED_VERSION}")
    return torchsig, TorchSigIterableDataset, TorchSigDefaults


def _class_seed(master_seed: int, split: str, class_idx: int) -> int:
    ss = np.random.SeedSequence(entropy=int(master_seed), spawn_key=(zlib.crc32(split.encode()), 1, class_idx))
    return int(ss.generate_state(1)[0])


def generate_split(cfg: dict, split: str):
    torchsig, Dataset, Defaults = _import()
    n = cfg["samples_per_example"]
    per = cfg["examples_per_class"][split]
    classes = cfg["classes"]
    for c in classes:
        if c not in CLASS_MAP:
            raise ValueError(f"class {c!r} has no TorchSig mapping")
    per_class = []
    for ci, c in enumerate(classes):
        md = Defaults().default_dataset_metadata
        md.update(num_iq_samples_dataset=n, num_signals_min=1, num_signals_max=1,
                  fft_size=64, fft_stride=64,
                  snr_db_min=cfg["snr_db"][0], snr_db_max=cfg["snr_db"][1],
                  signal_duration_in_samples_min=n, signal_duration_in_samples_max=n,
                  signal_center_freq_min=0, signal_center_freq_max=0,
                  bandwidth_min=1_000_000, bandwidth_max=2_500_000)
        seed = _class_seed(cfg["master_seed"], split, ci)
        ds = Dataset(signal_generators=[CLASS_MAP[c]], metadata=md, target_labels=None, seed=seed)
        items = []
        for _ in range(per):
            sig = next(ds)
            comp = sig.component_signals[0]
            items.append((np.asarray(sig.data, dtype=np.complex64), {
                "torchsig_class": comp["class_name"], "snr_db": float(comp["snr_db"]),
                "bandwidth_hz": float(comp["bandwidth"]), "center_freq_hz": float(comp["center_freq"]) + 0.0,
                "sample_rate_hz": float(comp["sample_rate"]), "seed": seed}))
        per_class.append(items)
    ss = np.random.SeedSequence(entropy=int(cfg["master_seed"]), spawn_key=(zlib.crc32(split.encode()), 0))
    order = np.random.Generator(np.random.PCG64(ss)).permutation(np.repeat(np.arange(len(classes)), per))
    cursor = [0] * len(classes)
    iq = np.empty((len(order), n), dtype=np.complex64)
    params = []
    for i, cid in enumerate(order):
        x, p = per_class[cid][cursor[cid]]
        cursor[cid] += 1
        iq[i] = x
        params.append({**p, "index": i, "class_id": int(cid), "class_name": classes[cid]})
    return iq, order.astype(np.int32), np.array([p["snr_db"] for p in params], np.float32), params, torchsig.__version__


def generate(cfg: dict, out_dir: Path) -> list[dict]:
    out_dir.mkdir(parents=True, exist_ok=True)
    sidecars = []
    for split in ("train", "val", "test"):
        if cfg["examples_per_class"].get(split, 0) <= 0:
            continue
        iq, cid, snr, params, ts_version = generate_split(cfg, split)
        sha = write_dataset(out_dir / f"{split}.kncds", iq, cid, snr, len(cfg["classes"]))
        buf = io.StringIO()
        w = csv.writer(buf, lineterminator="\n")
        w.writerow(COLUMNS)
        for p in params:
            w.writerow([repr(p[c]) if isinstance(p[c], float) else p[c] for c in COLUMNS])
        csv_bytes = buf.getvalue().encode()
        (out_dir / f"{split}.impairments.csv").write_bytes(csv_bytes)
        side = {
            "format": "KNCRFDS", "format_version": "1.0", "split": split, "file": f"{split}.kncds",
            "sha256": sha, "num_examples": int(iq.shape[0]), "samples_per_example": int(iq.shape[1]),
            "sample_format": "complex64_interleaved_iq_le", "sample_rate_hz": params[0]["sample_rate_hz"],
            "class_map": {c: i for i, c in enumerate(cfg["classes"])},
            "torchsig_class_map": {c: CLASS_MAP[c] for c in cfg["classes"]},
            "generator": {"name": "torchsig", "version": ts_version, "adapter": "knc_rf_lab.torchsig_adapter"},
            "config": cfg,
            "seeds": {"master_seed": cfg["master_seed"],
                      "derivation": "per (split, class): SeedSequence(master, spawn_key=(crc32(split),1,class)).generate_state(1)[0]"},
            "software": {"knc_rf_lab": __version__, "numpy": np.__version__,
                         "python": platform.python_version(), "torchsig": ts_version},
            "impairments": {"file": f"{split}.impairments.csv",
                            "sha256": hashlib.sha256(csv_bytes).hexdigest(), "columns": list(COLUMNS)},
        }
        (out_dir / f"{split}.json").write_text(json.dumps(side, indent=2, sort_keys=True) + "\n")
        sidecars.append(side)
    return sidecars
