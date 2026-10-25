# Benchmark methodology

## Components

- **`knc_bench`** (`csrc/tools/knc_bench.c`): one C driver that times one
  configuration and prints one JSON record. It records its own metadata
  (compiler, build flags, git commit at build time, CPU model, hypervisor flag,
  OS, OpenMP version and environment, dataset SHA-256), so it works on the
  coprocessor without Python. The same source becomes the host baseline and the
  KNC native binary (`mic/native/Makefile`); only `KNC_EXEC_MODE` and the flags
  change.
- **`python -m knc_rf_lab.benchmark`** (`python/knc_rf_lab/benchmark.py`):
  sweeps kernels × precision × batch × threads, adds host context
  (`lscpu`, Python/NumPy versions, git commit), adds explicit
  skipped/unavailable records, validates the result file against the schema,
  and can plot (`--plot`, needs matplotlib).

```
python -m knc_rf_lab.benchmark --dataset data/dev/test.kncds --out results/bench-host.json --plot
build/knc_bench --kernel psd --dataset data/dev/test.kncds --batch 256 --threads 4 --precision f32
```

## Inputs

| option | meaning |
|--------|---------|
| `--kernel` | `cmul magsq meanpow psd fft xcorr cfo features` (definitions in `docs/kernels.md`) |
| `--dataset F` / `--synthetic --samples S` | KNCRFDS file (S taken from the header) or a deterministic synthetic tone+noise buffer |
| `--batch B` | examples per kernel call: sweep 1, 16, 64, 256, 1024. If B > N, examples are reused from the start and `input.wrapped = true`. |
| `--threads T` | `omp_set_num_threads(T)`. Host sweep: 1, 2, 4, …, nproc. 30/60/120/240 are MIC-only and recorded as `skipped` on the host. |
| `--warmup W`, `--reps R` | untimed warm-up calls, then R timed calls |
| `--precision` | `f32` or `f64`. `features` always reads float32 I/Q and computes in double; it is run with `f32` only. |
| `--xcorr-len L` | template length for `xcorr` (default 64; the template is the first L samples of example 0) |

Kernel-specific setup: `cmul` multiplies by a fixed unit-magnitude tone; `psd`
uses the Hann window; `fft` copies fresh input into the work buffer before each
rep (it is in-place) and the copy is **outside** the timed region.

## Timing

- Clock: `clock_gettime(CLOCK_MONOTONIC)` (`csrc/src/timing.c`).
- Phases (seconds):
  - `init`: buffer allocation (64-byte aligned, `posix_memalign`), FFT plan
    creation, dataset open and full validation including CRC.
  - `load`: reading the batch from the file (or synthesizing it), plus
    float→double conversion for `f64`.
  - `transfer`: host↔coprocessor transfer. **`null` on the host and in
    native MIC mode.** Only the offload path (`mic/offload/`) fills it.
  - `kernel`: per-rep wall time of the kernel call; the record keeps every
    sample plus `median`, `min`, `max`, `q25`, `q75`, `iqr = q75 − q25`.
    Percentiles use linear interpolation (NumPy's default `linear` method).
  - `post`: error check against the reference, plus hashing the dataset.
- Throughput uses the median: `complex_samples_per_s = B·S / median`,
  `examples_per_s = B / median`.

## Accuracy check

After timing, the first `min(B, 4)` examples are recomputed by naive
double-precision C loops (an O(N²) DFT for `fft`/`psd`; direct sums for the
rest), independent of the kernel code. The record stores `max_abs` and
`max_rel` and names the metric:

- `cmul`, `magsq`: elementwise `|out − ref| / |ref|`
- `meanpow`: per example relative; `cfo`: per example absolute (cycles/sample)
- `fft`, `psd`, `xcorr`: per example normwise `‖out − ref‖₂ / ‖ref‖₂`
- `features`: `{"status": "unavailable", ...}`. There is no independent C
  reference; features are checked against NumPy in `tests/test_c_core.py`.

## Record schema

The schema is the JSON Schema document
`python/knc_rf_lab/schemas/benchmark.schema.json`, validated by
`knc_rf_lab.schema` (subset validator, `docs/decisions.md` D-03) in
`tests/test_benchmark.py` and on every orchestrator run.

Results file:

```
{
  "schema_version": "1.0",
  "created_utc": "...",
  "host_context": {"note", "python", "numpy", "lscpu": {...}, "git_commit"},
  "records": [ <ok_record> | <missing_record>, ... ]
}
```

`ok_record` (all keys required unless marked optional):

| key | type | notes |
|-----|------|-------|
| `schema_version` | `"1.0"` | |
| `status` | `"ok"` | |
| `simulated` | `false` | always false: there are no simulated results |
| `execution_mode` | `host` \| `mic-native` \| `mic-offload` | set at compile time (`KNC_EXEC_MODE`) |
| `kernel`, `precision` | enum | |
| `batch`, `samples_per_example`, `threads`, `warmup`, `reps` | int | |
| `xcorr_len` | int | optional, `xcorr` only |
| `input` | `{kind, path, sha256, wrapped}` | `sha256` is computed by the binary itself |
| `timing` | `{unit:"s", init, load, transfer, kernel:{median,min,max,q25,q75,iqr,samples[]}, post}` | `transfer` may be `null` |
| `throughput` | `{complex_samples_per_s, examples_per_s}` | |
| `error` | `{reference, examples_checked, metric, max_abs, max_rel}` or `{status:"unavailable", reason}` | |
| `metadata` | `{timestamp_utc, cpu_model, cpu_hypervisor_flag, online_cpus, os{sysname,release,machine}, compiler, build_flags, openmp{version,max_threads,team_size?,env{OMP_*,KMP_*}}, toolchain?{name,vector_isa}, git_commit}` | `team_size` is measured in a real parallel region (observed, not requested). `toolchain.name`: `cmake-host`, `icc-mmic`, `gcc-k1om`, `icc-offload`; `vector_isa` says whether the KNC vector unit can be in use. **Records with different `toolchain.name` are different experiments**: gcc-k1om KNC code is expected to be scalar |
| `offload` | `{pattern, chunks, device, kernel_includes_transfer?, requested_threads?, mic_team_size?}` | optional, offload records only. `kernel_includes_transfer: true` (per_call, chunked) means `timing.kernel` is end to end and `timing.transfer` is null |

`missing_record`: `{schema_version, status: "skipped" | "unavailable",
reason, [execution_mode], [kernel], [threads]}`. A measurement that was not
taken is always one of these, never a number.

## Caveats

- Results from this repository's cloud container come from a **4-vCPU KVM
  guest** (`cpu_hypervisor_flag: true`, `lscpu` Hypervisor vendor KVM). Other
  tenants and vCPU scheduling add noise. They show that the pipeline works;
  they are not the lab host baseline.
- Small batches (B = 1) measure call and OpenMP fork/join overhead more than
  compute.
- `git_commit` is captured when CMake configures. `make` reconfigures each
  time, so it is current; a `-dirty` suffix marks uncommitted tracked changes.
- Kernels are the portable C99 code, compiled with `-O3 -march=native` (gcc
  auto-vectorization). No intrinsics. A host-optimized variant belongs in a
  separate directory and would be a different record family.
