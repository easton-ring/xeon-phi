# knc-rf-lab

A reproducible research project that measures how much an **Intel Xeon Phi
5110P (Knights Corner, KNC)** speeds up RF DSP kernels and a small RF
modulation classifier, compared with a modern host CPU.

**Status: Milestone 1.** The pipeline is built and verified on a host CPU
(a cloud VM). Nothing has run on a Xeon Phi yet. MIC build files and procedures
exist and are marked UNVERIFIED. See [`docs/status.md`](docs/status.md) for
what is verified and the evidence.

## What is here

| path | contents |
|------|----------|
| `python/knc_rf_lab/` | host-only Python: config, deterministic generator, dataset reader/writer, features (NumPy), benchmark orchestration, classifier training/evaluation/export, TorchSig adapter |
| `csrc/` | portable C99 core with a plain C ABI: dataset reader, CRC-32, SHA-256, DSP kernels, radix-2 FFT, features, model inference; tools `knc_validate`, `knc_bench`, `knc_infer`; CTest tests |
| `mic/` | KNC native and offload build files and run scripts; `mic/guest/` MPSS guest VM kickstart, libvirt XML and install scripts (all UNVERIFIED, not built by default) |
| `configs/` | dataset configs: `dev.yaml` (8 classes × 200/split × 1024 samples), `shifted.yaml` (generalization test), `tiny.yaml` (unit tests), `torchsig_tiny.yaml` |
| `docs/` | format, kernels, features, benchmark methodology, architecture, MPSS / QEMU passthrough notes, decisions, status |
| `tests/` | pytest, including cross-language C-vs-NumPy checks |
| `results/examples/` | small committed example outputs from this environment |

## Quickstart

Needs Python ≥ 3.10, a C99 compiler with OpenMP, CMake ≥ 3.13 and make.

```sh
python3 -m venv .venv
.venv/bin/pip install -e ".[dev]"     # numpy, pyyaml + pytest, scikit-learn, matplotlib
make test                             # CMake build, CTest, pytest
make demo                             # whole pipeline, ~35 s on 4 vCPUs
```

`make demo` runs:

1. **generate**: `python -m knc_rf_lab generate --config configs/dev.yaml --out data/dev`
   (and `configs/shifted.yaml` → `data/shifted`)
2. **validate**: `python -m knc_rf_lab validate data/dev/*.kncds …` and `build/knc_validate …`
3. **benchmark**: `python -m knc_rf_lab.benchmark --dataset data/dev/test.kncds --out results/bench-host.json --plot`
4. **train**: `python -m knc_rf_lab.classify train --data data/dev --out artifacts/models`
5. **evaluate**: `python -m knc_rf_lab.classify evaluate … --shifted data/shifted/test.kncds` → `results/eval/metrics.json`, `confusion_*.csv`
6. **C inference check**: `python -m knc_rf_lab.classify infer_check …` → `results/eval/infer_check.json`

You can also run one benchmark configuration directly:

```sh
build/knc_bench --kernel psd --dataset data/dev/test.kncds --batch 256 --threads 4 --precision f32
```

## Documentation

- [Dataset format (KNCRFDS v1)](docs/dataset-format.md)
- [DSP kernels, conventions and error bounds](docs/kernels.md)
- [Features and model export](docs/features.md)
- [Benchmark methodology and JSON schema](docs/benchmark-methodology.md)
- [Architecture: host, MIC native, MIC offload](docs/architecture.md)
- [MPSS 3.8 compatibility](docs/mpss-compatibility.md) · [QEMU/VFIO passthrough](docs/qemu-passthrough.md)
- [Porting the KNC driver to a modern kernel (plan)](docs/kernel-port.md)
- [TorchSig adapter](docs/torchsig.md) · [Decisions](docs/decisions.md) · [Status matrix and Milestone 2 checklist](docs/status.md)

## Honesty rules

Results labeled "Xeon Phi 5110P" come only from that card. Missing
measurements are written as `{"status": "unavailable", "reason": …}`. No result
is simulated (`"simulated": false` is a schema constant). MIC binaries fail
loudly when no coprocessor is present. Scripts never bind devices to VFIO,
unload drivers, flash firmware or change boot configuration; those steps are
documented for an operator.

License: MIT.
