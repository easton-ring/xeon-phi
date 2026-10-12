"""Benchmark orchestration: sweep knc_bench over kernels/batches/threads/precisions.

    python -m knc_rf_lab.benchmark --dataset data/dev/test.kncds --out results/bench-host.json

Every record comes from the C driver, which records its own metadata. This
module adds the host context, explicit "skipped"/"unavailable" records for
configurations that cannot run here (MIC thread counts, MIC execution modes),
and validates the file against schemas/benchmark.schema.json.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

import numpy as np

from .cbin import REPO, find_binary
from .schema import validate

KERNELS = ("cmul", "magsq", "meanpow", "psd", "fft", "xcorr", "cfo", "features")
BATCHES = (1, 16, 64, 256, 1024)
MIC_THREADS = (30, 60, 120, 240)


def host_threads(nproc: int | None = None) -> list[int]:
    nproc = nproc or os.cpu_count() or 1
    t, out = 1, []
    while t < nproc:
        out.append(t)
        t *= 2
    out.append(nproc)
    return out


def lscpu() -> dict:
    try:
        txt = subprocess.run(["lscpu"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        return {"status": "unavailable", "reason": str(e)}
    keep = ("Architecture", "Model name", "CPU(s)", "Thread(s) per core", "Core(s) per socket",
            "Socket(s)", "Hypervisor vendor", "Virtualization type", "L1d cache", "L2 cache", "L3 cache",
            "NUMA node(s)")
    out = {}
    for line in txt.splitlines():
        k, _, v = line.partition(":")
        if k.strip() in keep:
            out[k.strip()] = v.strip()
    return out


def git_commit() -> str:
    try:
        c = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True,
                           check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain", "--untracked-files=no"], cwd=REPO,
                               capture_output=True, text=True).stdout.strip()
        return c + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def run_one(exe: Path, kernel: str, dataset: Path | None, batch: int, threads: int, precision: str,
            reps: int, warmup: int, samples: int = 1024) -> dict:
    cmd = [str(exe), "--kernel", kernel, "--batch", str(batch), "--threads", str(threads),
           "--precision", precision, "--reps", str(reps), "--warmup", str(warmup)]
    cmd += ["--dataset", str(dataset)] if dataset else ["--synthetic", "--samples", str(samples)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        return {"schema_version": "1.0", "status": "unavailable", "kernel": kernel, "threads": threads,
                "execution_mode": "host", "reason": f"knc_bench failed (rc={p.returncode}): {p.stderr.strip()}"}
    return json.loads(p.stdout)


def sweep(dataset: Path | None, kernels, batches, threads, precisions, reps, warmup,
          samples: int = 1024, log=print) -> dict:
    exe = find_binary("knc_bench")
    nproc = os.cpu_count() or 1
    records = []
    for k in kernels:
        for prec in precisions:
            if k == "features" and prec == "f64":
                continue  # features always read f32 and compute in f64
            for b in batches:
                for t in threads:
                    r = run_one(exe, k, dataset, b, t, prec, reps, warmup, samples)
                    records.append(r)
                    if r["status"] == "ok":
                        log(f"{k:8s} {prec} B={b:5d} T={t:3d}  median={r['timing']['kernel']['median'] * 1e3:9.4f} ms"
                            f"  {r['throughput']['complex_samples_per_s'] / 1e6:10.1f} MS/s")
                    else:
                        log(f"{k:8s} {prec} B={b:5d} T={t:3d}  {r['status']}: {r['reason']}")
    for t in MIC_THREADS:
        records.append({"schema_version": "1.0", "status": "skipped", "execution_mode": "host", "threads": t,
                        "reason": f"MIC-only thread count (5110P: 60 cores x 4 threads); "
                                  f"this host has {nproc} online CPUs"})
    for mode in ("mic-native", "mic-offload"):
        records.append({"schema_version": "1.0", "status": "unavailable", "execution_mode": mode,
                        "reason": "no Xeon Phi coprocessor in this environment; see docs/status.md "
                                  "for the Milestone 2 procedure"})
    doc = {
        "schema_version": "1.0",
        "created_utc": _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "host_context": {
            "note": "Measured where the orchestrator ran. If lscpu reports a hypervisor, this is a "
                    "virtual machine and NOT the lab host; numbers are not comparable to bare metal.",
            "python": platform.python_version(),
            "numpy": np.__version__,
            "lscpu": lscpu(),
            "git_commit": git_commit(),
        },
        "records": records,
    }
    validate(doc)
    return doc


def plot(doc: dict, out_png: Path) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    ok = [r for r in doc["records"] if r["status"] == "ok"]
    kernels = sorted({r["kernel"] for r in ok})
    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    for k in kernels:
        rs = [r for r in ok if r["kernel"] == k and r["precision"] == "f32"]
        if not rs:
            continue
        bmax = max(r["batch"] for r in rs)
        tmax = max(r["threads"] for r in rs)
        th = sorted((r for r in rs if r["batch"] == bmax), key=lambda r: r["threads"])
        axes[0].plot([r["threads"] for r in th], [r["throughput"]["complex_samples_per_s"] for r in th],
                     marker="o", label=k)
        bt = sorted((r for r in rs if r["threads"] == tmax), key=lambda r: r["batch"])
        axes[1].plot([r["batch"] for r in bt], [r["throughput"]["complex_samples_per_s"] for r in bt],
                     marker="o", label=k)
    axes[0].set(xlabel="threads (largest batch)", ylabel="complex samples/s", yscale="log",
                title="Thread sweep, f32")
    axes[1].set(xlabel="batch (most threads)", ylabel="complex samples/s", xscale="log", yscale="log",
                title="Batch sweep, f32")
    axes[1].legend(fontsize=7, loc="center left", bbox_to_anchor=(1.0, 0.5))
    fig.suptitle(f"host: {doc['host_context']['lscpu'].get('Model name', '?')} "
                 f"(hypervisor: {doc['host_context']['lscpu'].get('Hypervisor vendor', 'none')})", fontsize=9)
    fig.tight_layout()
    fig.savefig(out_png, dpi=110)


def _csv_ints(s: str) -> list[int]:
    return [int(v) for v in s.split(",") if v]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m knc_rf_lab.benchmark")
    ap.add_argument("--dataset", type=Path, help="KNCRFDS file (default: synthetic buffer)")
    ap.add_argument("--samples", type=int, default=1024, help="samples per example for synthetic input")
    ap.add_argument("--kernels", default=",".join(KERNELS))
    ap.add_argument("--batches", default=",".join(map(str, BATCHES)))
    ap.add_argument("--threads", default=None, help="default: 1,2,4,...,nproc")
    ap.add_argument("--precisions", default="f32,f64")
    ap.add_argument("--reps", type=int, default=20)
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--plot", action="store_true", help="also write <out>.png (needs matplotlib)")
    a = ap.parse_args(argv)
    threads = _csv_ints(a.threads) if a.threads else host_threads()
    doc = sweep(a.dataset, a.kernels.split(","), _csv_ints(a.batches), threads,
                a.precisions.split(","), a.reps, a.warmup, a.samples)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(doc, indent=1) + "\n")
    print(f"wrote {a.out} ({sum(r['status'] == 'ok' for r in doc['records'])} measured, "
          f"{sum(r['status'] != 'ok' for r in doc['records'])} skipped/unavailable records); schema valid")
    if a.plot:
        png = a.out.with_suffix(".png")
        plot(doc, png)
        print(f"wrote {png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
