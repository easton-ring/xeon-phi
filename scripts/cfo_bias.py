"""Measure the lag-1 CFO estimator's error per class and SNR band on a dataset split.

Usage: python scripts/cfo_bias.py data/dev/test.kncds
"""

import csv
import sys
from pathlib import Path

import numpy as np

from knc_rf_lab import dsp_ref
from knc_rf_lab.dsformat import Dataset

path = Path(sys.argv[1] if len(sys.argv) > 1 else "data/dev/test.kncds")
ds = Dataset(path)
rows = list(csv.DictReader(open(path.with_name(path.stem + ".impairments.csv"))))
f = np.array([float(r["freq_offset"]) for r in rows])
cls = np.array([r["class_name"] for r in rows])
est = dsp_ref.cfo(np.asarray(ds.iq))
print("| class | SNR band (dB) | n | mean error | RMS error |")
print("|---|---|---:|---:|---:|")
for c in sorted(set(cls)):
    for lo, hi in ((10, 20.01), (0, 10), (-10, 0)):
        m = (cls == c) & (ds.snr_db >= lo) & (ds.snr_db < hi)
        e = est[m] - f[m]
        print(f"| {c} | [{lo}, {min(hi, 20):g}] | {m.sum()} | {e.mean():+.4f} | {np.sqrt(np.mean(e ** 2)):.4f} |")
