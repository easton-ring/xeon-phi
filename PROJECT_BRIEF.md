# knc-rf-lab — Session Brief

You are a senior HPC/DSP engineer with SDR and legacy Intel Xeon Phi Knights
Corner (KNC) experience. Build the first working milestone of **knc-rf-lab**, a
reproducible research project that measures how well an Intel Xeon Phi 5110P
speeds up RF DSP kernels and a small RF modulation classifier, compared with a
modern host CPU.

Implement it. Don't just describe it. Make routine engineering decisions
yourself and write them down in `docs/decisions.md`. Ask only when a decision
blocks progress or could be destructive.

---

## 0. Where you are running (read first)

This session runs in an **ephemeral cloud container**, not on the lab machine.

- Repo: `easton-ring/xeon-phi`, branch `claude/xeon-phi-rf-dsp-l8qh3t`. It
  contains only a stub `README.md`. Build the project at the **repo root**
  (don't nest it in a `knc-rf-lab/` subdirectory). The Python package is
  `knc_rf_lab`.
- Expected toolchain (check it, don't trust this list): Python 3.13, NumPy,
  gcc/g++, CMake, about 4 vCPUs. **No** PyTorch, TorchSig, scikit-learn, Intel
  compiler, MPSS, KVM or Xeon Phi.
- Outbound network goes through a proxy. `pip install` probably works. Large
  downloads (for example PyTorch) are slow and use up limited disk, so install
  the CPU-only wheel if you install it at all.
- The container is reclaimed when the session ends. **Commit and push at the
  end of every phase** with `git push -u origin claude/xeon-phi-rf-dsp-l8qh3t`.
  Don't open a PR unless asked.
- Hardware metadata recorded here describes a virtualized cloud CPU. Record it
  honestly. Never present it as the lab host.

The real lab, which this session can't reach, looks like this:

- **Host:** a modern Intel Linux machine with QEMU/KVM, the Phi assigned to a
  guest through VFIO, and Python/PyTorch/TorchSig.
- **Guest:** a RHEL/CentOS 7-era VM running Intel MPSS 3.8.x and the last
  Intel compiler with KNC support (believed to be Intel C++ 17.x / Parallel
  Studio XE 2017; **verify this**). The guest is an isolated research
  appliance.
- **Card:** a Xeon Phi 5110P (KNC, 60 cores × 4 threads, about 1.05 GHz,
  8 GB GDDR5, 512-bit IMCI, about 1 TFLOP/s peak FP64, passively cooled so it
  needs forced airflow).

## 1. Non-negotiable honesty rules

1. KNC is not KNL, not a GPU and not a CUDA/SYCL/oneAPI target. Never
   substitute any of them in a result labeled "Xeon Phi 5110P".
2. Never fabricate measurements, and never label simulated or host results as
   MIC results. Missing measurements are recorded as
   `{"status": "unavailable", "reason": "..."}`.
3. No silent fallback from MIC to CPU. MIC and offload binaries must **fail
   loudly** when no coprocessor is present. Never use an offload mode that
   quietly runs on the host.
4. Keep a status matrix in `docs/status.md`. It lists every component as
   **verified here**, **verified on hardware** or **unverified**, with the
   evidence: the command run and where its output is.
5. Don't invent APIs, compiler flags or pragmas. Check TorchSig against the
   installed package or its source. Check Intel KNC flags and `#pragma
   offload` syntax against historical Intel documentation, and cite the source
   in the docs. If you can't verify something, mark it `UNVERIFIED`.
6. Never write scripts that automatically bind PCI devices to VFIO, unload host
   drivers, flash card firmware (`micflash`) or change boot configuration.
   Document those steps for an operator to review and run.

## 2. Scope of this session: Milestone 1 only

Milestone 1 is everything that can be built and verified **without the Phi**.
Prioritize one small path that works end to end over many partial features.

### Acceptance criteria (all must pass in this container)

1. `pip install -e ".[dev]"` works in a fresh venv. Core dependencies are only
   `numpy` and `pyyaml`. Extras: `[ml]` (scikit-learn), `[plot]` (matplotlib),
   `[torchsig]`, `[dev]` (pytest plus the above where cheap).
2. The built-in deterministic generator creates a small labeled dataset in
   seconds, and the output is byte-identical for a fixed config.
3. The TorchSig adapter is either tested against an installed TorchSig, or
   isolated behind one module, marked `UNVERIFIED`, and skipped in tests with
   a stated reason.
4. Python and C both read and validate the dataset, including the corruption
   cases in §4.
5. The C DSP kernels pass correctness tests against NumPy (float64) with
   documented tolerances.
6. The host benchmark writes schema-valid JSON results with full metadata.
7. A feature-based classifier trains on the train split and reports accuracy
   on held-out test data, broken down by class and by SNR. A C inference
   routine reproduces the Python predictions.
8. `make demo` runs the whole pipeline (generate → validate (Python and C) →
   benchmark → train → evaluate → C inference check) in a few minutes on a
   laptop.
9. `make test` runs pytest and CTest.
10. The docs describe the native-MIC and offload paths, with every unverified
    step marked.

**Not in this session:** hand-written IMCI intrinsics, running anything on
MIC, neural-network models on KNC, and large datasets. Leave stubs only where
a doc explains what goes there.

## 3. Engineering decisions already made

You may change one of these if you find a concrete reason. Record the reason
in `docs/decisions.md`.

- **The portable compute core is C99** (no VLAs, no C11 atomics, no compiler
  extensions outside `#ifdef`). It lives in `csrc/` (or `cpp/`, your choice)
  with a plain C ABI. Reason: icc 17 `-mmic` and old gcc build it unchanged,
  and the same source becomes the host baseline (modern gcc `-O3
  -march=native -fopenmp`) and the KNC native build. That makes host-vs-MIC a
  comparison of the same code. A host-only "optimized" variant can come later,
  in a separate directory.
- **Timing** uses `clock_gettime(CLOCK_MONOTONIC)` or `omp_get_wtime()`.
  Don't use C++ `<chrono>` in shared code.
- **Threading** uses OpenMP only. The thread count comes from the CLI or
  environment. Host sweeps run `1,2,4,…,nproc`. The `30/60/120/240` points are
  MIC-only and skipped elsewhere with a reason.
- **C tests** are plain executables registered with CTest. Don't add
  GoogleTest or other downloaded frameworks.
- **Cross-language checks.** pytest builds or finds the C binaries and
  compares their output with NumPy and the Python reader.
- **Layout.** Keep these three parts separate:
  - host-only Python: `python/knc_rf_lab/`
  - portable C: `csrc/`
  - MIC-specific build files and offload sources: `mic/`

  The tree in the original idea list (README, LICENSE, pyproject, Makefile,
  `configs/`, `docs/`, `scripts/`, `tests/`, `results/`, `artifacts/`) is a
  guide. Don't create empty placeholder files. Generated data and results stay
  git-ignored except small committed examples.
- **License:** MIT, unless you find a reason to pick otherwise.

## 4. Dataset format (proposed v1; refine and document in `docs/dataset-format.md`)

Each split is its own file: `train.kncds`, `val.kncds`, `test.kncds`. Each has
a JSON sidecar.

**Header.** 64 bytes, fixed, little-endian, written field by field. Never
`fwrite` a struct.

| bytes | type    | field |
|-------|---------|-------|
| 8     | char[8] | magic `"KNCRFDS\0"` |
| 2     | u16     | `version_major` |
| 2     | u16     | `version_minor` |
| 4     | u32     | `header_size` (64) |
| 8     | u64     | `num_examples` |
| 4     | u32     | `samples_per_example` |
| 4     | u32     | `num_classes` |
| 4     | u32     | `sample_format` (1 = complex float32, interleaved I/Q) |
| 4     | u32     | `flags` |
| 8     | u64     | `labels_offset` |
| 8     | u64     | `payload_offset` |
| 4     | u32     | `payload_crc32` |
| 4     | u32     | `header_crc32` (computed with this field zeroed) |

**Labels section.** `num_examples × {i32 class_id, f32 snr_db}`.

**Payload.** Starts on a 64-byte boundary (KNC cache-line and 512-bit vector
alignment). It holds `num_examples × samples_per_example × 2` float32 values:
I0, Q0, I1, Q1, …

**JSON sidecar.** Format version, class-name → id map, sample rate, split
name, generator name and version, full generation config, per-split seeds,
software versions, SHA-256 of the binary file, and per-example impairment
parameters if they're small (otherwise a separate `.npz`/CSV).

**Readers.** The Python reader uses `numpy.memmap`. The C reader validates the
header and maps or streams examples in batches. Both reject: bad magic,
unsupported version, size or offset fields that overflow (use checked u64
arithmetic), offsets past end of file, truncated payload, misaligned payload,
and CRC mismatch. Write a test for each case.

## 5. Dataset generation

- **Built-in generator** (always available, NumPy only, deterministic): BPSK,
  QPSK, 8PSK, 16-QAM, 64-QAM, 2-FSK, 4-FSK, and optionally an OFDM-with-QAM
  class. It uses RRC pulse shaping where relevant, AWGN at configured SNR, and
  optional frequency offset, phase offset, fractional timing offset and simple
  flat or Rayleigh fading. Every example records the impairments applied to
  it.
- **Defaults:** 1024 samples per example (configurable up to at least 4096),
  SNR uniform over −10…+20 dB, about 200 examples per class per split for the
  dev config. The YAML config lives in `configs/`.
- **TorchSig adapter.** Try `pip install torchsig` (with CPU-only torch). If it
  installs, read its actual API in the installed source and generate only the
  classes it really supports, with their real names mapped to ours. If it
  doesn't install or doesn't work on Python 3.13, record the exact error, keep
  the adapter behind one module, and mark it `UNVERIFIED`. Don't spend more
  than about 20 minutes on this.
- **Hygiene.** Every example comes from its own random symbol sequence and
  channel draw. Train, val and test use disjoint seed streams derived from a
  master seed and split name. Classes are balanced. A separate
  "shifted-channel" test config (different impairment ranges) is supported for
  generalization tests. Document how far reproducibility holds across NumPy
  versions.

## 6. DSP kernels (C99, with float32 and float64 variants where meaningful)

1. Complex elementwise multiply.
2. Magnitude squared.
3. Mean power per example.
4. Batched power spectrum: periodogram `|FFT(w·x)|² / (N·Σw²/N)` with an
   optional Hann window. Document the exact normalization.
5. Cross-correlation, direct O(N·L), with documented lag range and conjugation
   convention.
6. Iterative radix-2 FFT for power-of-two N. The forward transform uses
   `e^{-j2πkn/N}`, unnormalized. The inverse is scaled by 1/N. This matches
   `numpy.fft`.
7. Frequency-offset estimate from the phase of the lag-1 autocorrelation.
   Document its bias on modulated signals.

**For every kernel:**
- a defined interface, invalid-size handling and a scalar reference
- tests on zeros, random input, sizes at and around multiples of 16 floats
  (the 512-bit width), misaligned pointers, and NaN/Inf propagation where it
  matters
- error tolerances justified by precision and length (for example, a
  relative-error bound for float32 FFT that scales with log2 N)

## 7. Host benchmark

There is a single C driver (`knc_bench`), plus Python orchestration
(`python -m knc_rf_lab.benchmark`).

**Inputs:**
- kernel
- dataset path or synthetic buffer
- batch size (1, 16, 64, 256, 1024)
- thread count
- warm-up count and repetition count
- float32 or float64

**Output (JSON):**
- one record per configuration: median, min, IQR, complex samples/s,
  examples/s
- max error vs reference
- timings broken into phases: init, load, transfer (null on host), kernel,
  post
- metadata: CPU model, kernel/OS, compiler and version, build flags, OpenMP
  runtime, thread count, batch size, dataset SHA-256, git commit, execution
  mode (`host` | `mic-native` | `mic-offload`) and a `simulated: false` field

Document the JSON schema in `docs/benchmark-methodology.md` and validate
against it in tests.

## 8. Classification

- Features, with formulas in `docs/features.md`:
  - power mean and variance
  - normalized amplitude moments
  - higher-order cumulants (C20, C21, C40, C41, C42, …)
  - instantaneous phase-difference and frequency statistics
  - spectral moments
- Implement the features in **both** NumPy and C, and test that they agree.
- Train on the host with scikit-learn: logistic regression and a small MLP.
  Use models whose parameters export as plain arrays, plus the
  standardization parameters, to a simple binary or JSON file. Skip random
  forests for the C path.
- Write C inference for the exported model and check that it matches
  sklearn's predictions on the test set (identical labels, or documented
  tie-break differences).
- Report accuracy overall, per class, per SNR bin and on the shifted-channel
  test set. Save metrics as JSON and the confusion matrix as CSV.

## 9. MIC preparation (docs and build files, all marked UNVERIFIED)

- `docs/mpss-compatibility.md`
  - MPSS 3.8.x requirements: supported RHEL/CentOS 7 minor releases and
    kernels, card flash/SMC version matching, the compiler and MKL versions
    with KNC support, and where to get them now that Intel has retired them
  - the guest diagnostics `micinfo`, `micctrl -s`, `miccheck` and
    `ssh mic0`
  - Separate verified facts, with sources, from beliefs.
- `docs/qemu-passthrough.md`
  - host checklist: VT-x/VT-d, IOMMU kernel parameters, `lspci -nn`,
    `lspci -tv`, IOMMU groups, VFIO binding inspection
  - the card's large 64-bit BAR: above-4G decoding and the guest's 64-bit
    MMIO window
  - QEMU device assignment, cooling and power
  - Make clear that working VFIO attachment ≠ working MPSS.
  - recovery precautions and the risks of an end-of-life guest OS
  - Label each command as **[host]**, **[guest]** or **[card]**.
- `mic/native/`
  - a Makefile that builds the same C99 core and `knc_bench` with the KNC
    compiler (for example `icc -mmic -qopenmp …`, with flags checked against
    historical docs)
  - scripts to copy the binary and data to the card (`scp` / `micnativeloadex`)
    and run thread sweeps with `KMP_AFFINITY` / `KMP_PLACE_THREADS` settings
    documented as UNVERIFIED
- `mic/offload/`
  - one small offload program (magnitude squared or batched power) using the
    documented `__attribute__((target(mic)))` / `#pragma offload` model
  - data kept resident across calls (`alloc_if`/`free_if`)
  - transfer and compute timed separately
  - comparisons of one large batch vs many small batches, and repeated vs
    single offload
  - It must abort if the device count is 0.
  - It isn't built by the default host build.
- `docs/architecture.md` explains that native and offload are different
  execution modes with different runtime requirements.

## 10. Workflow

Work in phases. After each phase: run the tests, fix failures, update
`docs/status.md`, commit, push.

1. **Inspect:** check the environment and tools, and try TorchSig within its
   time limit. Write a short plan in `docs/decisions.md`. Spend about 10
   minutes, not more.
2. **Skeleton:** packaging, config loading, built-in generator, dataset
   writer and reader (Python), validation CLI, tests.
3. **C core:** C reader, kernels, FFT, CTest, cross-language pytest.
4. **Benchmark:** `knc_bench`, JSON schema, Python orchestration, host thread
   and batch sweep on a small config, optional plots.
5. **Classifier:** features (Python and C), training, evaluation, export, C
   inference check.
6. **Demo:** `make demo` and the README quickstart.
7. **MIC prep:** docs and `mic/` (§9).
8. **Validate:** fresh venv, `make test`, `make demo`, and a check that the
   result JSON has real metadata.

Avoid monolithic scripts, hardcoded absolute paths, unneeded dependencies,
huge default datasets, and abstractions that come before the first working
benchmark.

## 11. Final report (in chat and in `docs/status.md`)

- the files created
- install, generate, test, benchmark and demo commands, each confirmed to work
- test results, including skipped tests and why they were skipped
- what is verified here vs unverified
- known limitations
- the exact step-by-step checklist to reach **Milestone 2** (a real run on the
  5110P): guest setup → MPSS bring-up → native build and run → offload build
  and run → collecting results back into `results/`

Don't claim Milestone 2 without real hardware runs and captured output.
