# Status: Milestone 1 (host pipeline) complete; Milestone 2 (real 5110P run) not started

Legend:

- **verified here**: executed and passing in this project's cloud container,
  a 4-vCPU KVM guest (`Intel(R) Xeon(R) Processor @ 2.10GHz`, hypervisor flag
  set). It is **not** the lab host.
- **verified on hardware**: run on the real Xeon Phi 5110P with captured
  output. **Nothing is in this category yet.**
- **unverified**: written but never executed against its real target.

Evidence logs are in `results/examples/logs/` from the final validation run:
commit `e4baf64`, fresh venv, `make clean` first.

## Status matrix

| component | status | evidence (command → output) |
|-----------|--------|-----------------------------|
| Packaging: `pip install -e ".[dev]"` in a fresh venv; core deps numpy + pyyaml only | verified here | `python3 -m venv V && V/bin/pip install -e ".[dev]"` (16 s) |
| Config loading and validation | verified here | `pytest tests/test_dataset.py::test_config_validation` |
| Built-in generator: 8 classes, RRC, AWGN, CFO, phase, fractional timing, flat and Rayleigh fading, per-example impairment CSV | verified here | `make data` → 4800 dev examples in ~2 s; `tests/test_dataset.py` |
| Byte-identical output for a fixed config | verified here (same NumPy 2.5.4 build) | `test_generate_byte_identical`. The dev `test.kncds` SHA-256 `bd43d304…` was identical in two separate venvs. Cross-version limits: `docs/dataset-format.md` |
| Disjoint split seed streams, balanced classes, unique examples | verified here | `test_split_seed_streams_disjoint`, `test_examples_unique`, `test_reader_roundtrip` |
| Shifted-channel test set | verified here | `configs/shifted.yaml` → `data/shifted/test.kncds`; evaluated in `make demo` |
| Python reader (memmap) plus 20 corruption cases | verified here | `pytest tests/test_dataset.py -k corruption` |
| C reader plus the same 20 cases (via `knc_validate`) and an equivalent in-C set | verified here | `pytest tests/test_c_core.py -k validator`; CTest `test_kncds` |
| CRC-32, SHA-256 in C | verified here | CTest `test_hash` (standard test vectors); C SHA-256 = sidecar SHA-256 (`test_c_validator_ok`) |
| C kernels f32/f64 vs NumPy float64 (cmul, magsq, mean power, xcorr, CFO, FFT, PSD), with documented bounds | verified here | `pytest tests/test_c_core.py`; CTest `test_kernels`, `test_fft` (sizes around multiples of 16 floats, misaligned pointers, zeros, NaN/Inf, invalid sizes); bounds in `docs/kernels.md` |
| CFO estimator bias, measured | verified here | `python scripts/cfo_bias.py data/dev/test.kncds` → table in `docs/kernels.md` |
| Strict C99 (gcc, clang, `-Werror`), no-OpenMP build | verified here | `make portability` → `results/examples/logs/make-portability.log` |
| `knc_bench` plus schema-valid JSON with full metadata | verified here | `pytest tests/test_benchmark.py`; `make bench` → `results/examples/bench-host.json` (135 measured records, 4 `skipped` MIC thread counts, 2 `unavailable` MIC modes) |
| Host thread × batch sweep and plot | verified here (VM numbers only) | `results/examples/bench-host.json`, `results/examples/bench-host.png` |
| Features: NumPy and C agree | verified here | `test_features_c_vs_numpy` (rtol = atol = 1e-9, N = 256 and N = 200) |
| Training (logreg, MLP), evaluation per class / SNR / shifted set, confusion CSV | verified here | `make train evaluate` → `results/examples/eval/` |
| C inference reproduces sklearn | verified here | `make infer-check` → 0 / 1600 mismatches for both models, max \|Δlogit\| 2.6e-13 / 1.8e-12 (`results/examples/eval/infer_check.json`) |
| `make demo` (generate → validate Py+C → bench → train → evaluate → C inference) | verified here | 37 s wall time; `results/examples/logs/make-demo.log` |
| `make test` (CTest + pytest) | verified here | CTest 6/6; pytest 105 passed, 1 skipped; `results/examples/logs/make-test.log` |
| TorchSig adapter (2.2.0) | verified here, **separate venv only** | `results/examples/torchsig_adapter_test.log`; skipped in the dev venv (reason printed by `pytest -rs`); see `docs/torchsig.md` |
| `mic/native/Makefile` (icc `-mmic`) | **unverified** | only `make -n` dry run; no icc here |
| `mic/native/deploy.sh`, `run_sweep.sh` | **unverified** | `sh -n` and `shellcheck -S warning` only |
| MPSS guest VM files (`mic/guest/`: kickstart, libvirt XML, MPSS install and diagnostics scripts) | **unverified** | `ksvalidator -v RHEL7`, `xmllint`, `sh -n`, `shellcheck -S warning`. No VM was built: no KVM, card or vault.centos.org access here |
| `make -C mic/guest check` (C tests via plain gcc Makefile) | verified here with gcc 13; **unverified** with EL7 gcc 4.8 | `make -C mic/guest check` → all 6 C tests ok |
| `KNC_REQUIRE_MIC` guards (compile-time `__MIC__`, runtime `uname -m == k1om`) | compile-time guard verified here (fires with gcc); runtime check unverified | `gcc -DKNC_REQUIRE_MIC -fsyntax-only csrc/tools/knc_bench.c` → `#error` |
| `mic/offload/offload_magsq.c` | **unverified** | C syntax only (`make -C mic/offload syntax-check`, gcc + stub header; offload pragmas not checked) |
| MPSS / compiler / passthrough facts | **unverified** on hardware; SOURCED items have URLs | `docs/mpss-compatibility.md`, `docs/qemu-passthrough.md` |
| Any MIC measurement | **none exist** | benchmark files record `{"status": "unavailable", "execution_mode": "mic-native"/"mic-offload", ...}` |

## Commands (all confirmed working here)

```sh
python3 -m venv .venv && .venv/bin/pip install -e ".[dev]"                    # install
.venv/bin/python -m knc_rf_lab generate --config configs/dev.yaml --out data/dev   # generate (~2 s)
.venv/bin/python -m knc_rf_lab validate data/dev/*.kncds --c                   # validate, Python + C
make test                                                                      # CTest + pytest
make bench          # or: python -m knc_rf_lab.benchmark --dataset data/dev/test.kncds --out results/bench-host.json --plot
make demo                                                                      # whole pipeline
make portability                                                               # strict C99 / no-OpenMP / offload syntax
```

## Test results (final run)

- CTest: 6/6 passed (`test_hash`, `test_kncds`, `test_kernels`, `test_fft`,
  `test_features`, `test_model`), with OpenMP and without.
- pytest: **105 passed, 1 skipped**. The skipped test is
  `tests/test_torchsig.py`: "torchsig not installed (optional [torchsig] extra;
  CPU-only torch index is unreachable here, see docs/torchsig.md)". The same
  test passes in the separate TorchSig venv.

## Example results (VM, not comparable to bare metal)

Dev test split, batch 1024, f32 unless noted, 10 reps, median throughput in
complex MS/s (`results/examples/bench-host.json`):

| kernel | prec | 1 thread | 4 threads | max rel. error vs double ref. |
|---|---|---:|---:|---:|
| cmul | f32 | 962 | 3681 | 1.0e-07 |
| magsq | f32 | 1838 | 7769 | 1.0e-07 |
| meanpow | f32 | 782 | 2660 | 8.5e-07 |
| psd (Hann) | f32 | 164 | 443 | 1.5e-07 |
| fft | f32 | 162 | 597 | 1.2e-07 |
| fft | f64 | 156 | 336 | 1.1e-15 |
| xcorr (L=64) | f32 | 26 | 113 | 1.6e-07 |
| cfo | f32 | 828 | 2715 | 2.5e-06 (absolute, cycles/sample) |
| features | (f32 in, f64 compute) | 15 | 35 | checked vs NumPy in tests |

Some 4-thread speedups exceed 4×. With 10 reps on a shared VM that is noise
or a cache effect, not a finding.

Classifier (`results/examples/eval/metrics.json`): test accuracy **0.666**
for both logreg and MLP; 0.88–0.92 at SNR ≥ 5 dB; 0.24 at −10…−5 dB. On the
shifted-channel set: 0.52 (logreg) and 0.51 (MLP). The main confusion is
16-QAM ↔ 64-QAM (`confusion_logreg_test.csv`). The MLP overfits (train 0.91 /
val 0.68).

## Known limitations

- All numbers come from a 4-vCPU cloud VM. They are not the lab host baseline.
- KNC code paths have never been compiled with icc or run. Several flags and
  pragmas are SOURCED only from search excerpts, and some are BELIEF or
  UNVERIFIED (`docs/mpss-compatibility.md`).
- `knc_bench` mic-native assumes `uname -m` is `k1om` on the card (inferred from forum-quoted `uname -a` output; not confirmed).
  If that is wrong, it fails loudly and the check must be fixed.
- The offload program is a separate binary covering one kernel (magsq). Its
  metadata is less complete than `knc_bench`'s; `timestamp_utc` is empty and
  the OS fields are partial.
- Generator reproducibility across NumPy versions and CPUs is not guaranteed
  (`docs/dataset-format.md`). Identity is the SHA-256.
- The TorchSig adapter passes only the SNR range; no impairments. It is tested
  only against 2.2.0 with a CUDA torch wheel, run on CPU.
- `features` has no independent C reference inside `knc_bench`; it is
  validated against NumPy in pytest.
- Kernels are plain C99 with compiler auto-vectorization; no IMCI or AVX-512
  intrinsics (by design for Milestone 1).
- The radix-2 FFT handles powers of two only; `psd`/`fft` reject other sizes.
  Features zero-pad.

## Milestone 2 checklist: first real run on the 5110P

Each step ends with captured output saved under `results/hw/<date>/`. A step
is "verified on hardware" only with that evidence.

**A. Host and passthrough** (operator; `docs/qemu-passthrough.md`)

1. [host] BIOS: VT-x, VT-d, above-4G decoding on. Card cooled (forced
   airflow) and powered. Save `lspci -nn -vvv -s <BDF>` (BAR0 8 GB assigned).
2. [host] `intel_iommu=on iommu=pt`; save `dmesg | grep -e DMAR -e IOMMU`
   and the IOMMU group listing.
3. [host] Operator binds 8086:2250 to vfio-pci; save `lspci -nnk -s <BDF>`.
4. [host] Install the guest from `mic/guest/centos73-mpss.ks` and define it
   from `mic/guest/knc-mpss-guest.xml` (`mic/guest/README.md`). Snapshot.

**B. Guest and MPSS bring-up** (`docs/mpss-compatibility.md`)

5. [guest] Confirm CentOS 7.3 / kernel 3.10.0-514 (`/root/ks-installed-versions.txt`).
6. [guest] `MPSS_TAR=... mic/guest/install-mpss-guest.sh`: installs MPSS 3.8.6,
   brings the card online and runs `collect-diagnostics.sh` (micinfo,
   micctrl -s, miccheck). If the flash doesn't match, the operator flashes
   per the readme and reruns. Snapshot.
7. [guest] `ssh mic0 uname -a; cat /proc/cpuinfo | head -30` → save. Confirm
   `uname -m` (fix the `KNC_REQUIRE_MIC` check if it is not `k1om`).
8. [guest] Install Intel C++ 17.0 (PSXE 2017); save `icc -V`. Re-check the
   UNVERIFIED rows of `docs/mpss-compatibility.md` against the 17.0
   documentation shipped with it, and update the doc with the findings.

**C. Native build and run**

9. [guest] `git clone` this repo at a tagged commit. `make -C mic/guest check`
   (C core and C tests with EL7's gcc 4.8: the "old gcc" portability check).
   Save the log. `make test` itself runs on the lab host (EL7.3 lacks
   CMake ≥ 3.13 and Python ≥ 3.10).
10. [guest] Copy or generate the dev dataset. Verify that its SHA-256 matches
    the sidecar (`knc-rf-lab validate`), or regenerate it and record the new
    SHA-256.
11. [guest] `make -C mic/native`; save the build log. Run
    `build-mic/knc_bench --kernel magsq --synthetic` **on the guest** and
    confirm it fails loudly (it must not run).
12. [guest→card] `MIC_LIBIOMP=<…/lib/mic/libiomp5.so> mic/native/deploy.sh
    data/dev/test.kncds`. The card-side `knc_validate` must print `"status":
    "ok"` with the same SHA-256.
13. [card] Smoke test: `ssh mic0 'cd /tmp/knc && LD_LIBRARY_PATH=. ./knc_bench
    --kernel fft --dataset test.kncds --batch 64 --threads 60'`. Check
    `execution_mode: "mic-native"`, `cpu_hypervisor_flag: false`, and
    `max_rel` within the `docs/kernels.md` bounds. Run the f32/f64 NaN/Inf
    expectations; if `-fp-model precise` does not preserve them, record that.
14. [guest] Watch the card temperature (`micinfo`) during
    `mic/native/run_sweep.sh test.kncds > results/hw/<date>/mic-native-raw.jsonl`.
15. [guest] `python -m knc_rf_lab.benchmark --collect
    results/hw/<date>/mic-native-raw.jsonl --out
    results/hw/<date>/bench-mic-native.json --note "5110P native, MPSS 3.8.6,
    icc 17.0.x"` (schema-validated).
16. [card] Native C inference: copy `artifacts/models/*.txt`, run
    `knc_infer`, and compare its CSV with the host's (expect identical labels;
    document differences).

**D. Offload build and run**

17. [guest] `make -C mic/offload`; save the log. Run once with
    `OFFLOAD_REPORT=2` to confirm transfers go to `mic:0`.
18. [guest] Stop the card (`micctrl --stop` or `service mpss stop`; verify
    the command) and run `build-mic/offload_magsq` → it must exit 3 (no
    device). Save this evidence of fail-loud. Restart the card.
19. [guest] `build-mic/offload_magsq --n-examples 1024 --chunks 16 >
    results/hw/<date>/offload-raw.jsonl`. Sweep `--chunks 1,4,16,64` and
    `MIC_ENV_PREFIX=MIC MIC_OMP_NUM_THREADS=236 MIC_KMP_PLACE_THREADS=59c,4t`.
    Collect with `--collect`.

**E. Host baseline and results**

20. [host] On the lab host (bare metal), run `make test` and `make bench` →
    `results/hw/<date>/bench-host.json`. Same commit, same dataset SHA-256.
21. Copy all `results/hw/<date>/` files into the repository. Add a comparison
    that pairs records only when kernel, precision, batch, dataset SHA-256 and
    commit match, and never mixes `execution_mode`s. Update this status
    matrix with "verified on hardware" rows that point at those files.

Milestone 2 is claimed only when steps 13, 15 and 19 have captured outputs in
the repository.
