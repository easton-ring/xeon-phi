# Architecture

```
            host-only Python                    portable C99 core                MIC-specific
        ┌──────────────────────┐          ┌─────────────────────────┐       ┌──────────────────────┐
        │ python/knc_rf_lab/   │          │ csrc/                   │       │ mic/native/          │
        │  config, generator   │ .kncds   │  kncds (reader), crc32, │       │  Makefile (icc -mmic)│
        │  dsformat (r/w)      ├─────────►│  sha256, kernels, fft,  │◄──────┤  deploy.sh,          │
        │  features (NumPy)    │          │  features, model        │ same  │  run_sweep.sh        │
        │  classify (sklearn)  │ model.txt│  tools: knc_validate,   │ source│ mic/offload/         │
        │  benchmark (sweeps)  ├─────────►│   knc_bench, knc_infer  │       │  offload_magsq.c     │
        │  torchsig_adapter    │◄─────────┤  JSON records           │       │  (LEO pragmas)       │
        └──────────────────────┘  results └─────────────────────────┘       └──────────────────────┘
```

- **Python** generates data, trains models, orchestrates runs and checks
  results. It never runs on the coprocessor.
- **The C99 core** is the only compute code. The same files build the host
  baseline (gcc, CMake) and the KNC native binaries (`icc -mmic`,
  `mic/native/Makefile`). Host-vs-MIC is therefore a comparison of the same
  source, with only compiler and flags differing. No intrinsics; no VLAs, C11
  atomics or compiler extensions outside `#ifdef`. `make portability` checks
  strict C99 with gcc and clang and builds without OpenMP.
- **Interfaces between the parts** are files: `.kncds` datasets (with a
  SHA-256 in every record), `KNCMODEL 1` text models, and JSON benchmark
  records validated against one schema.

## Three execution modes

| | `host` | `mic-native` | `mic-offload` |
|---|---|---|---|
| where `main()` runs | host CPU | on the card's own Linux (uOS), k1om | host (MPSS guest); marked regions run on the card |
| binary | `build/knc_bench` (gcc) | `build-mic/knc_bench` (`icc -mmic`), copied to the card | `build-mic/offload_magsq` (icc with offload; a fat binary with host and MIC code) |
| started by | shell | `ssh mic0 ./knc_bench …` or `micnativeloadex` | shell in the MPSS guest |
| data | read from disk | dataset copied to the card's RAM-backed filesystem; read there | host memory, copied over PCIe by the offload runtime (`in`/`out`/`nocopy`, `alloc_if`/`free_if`) |
| runtime needs | libgomp | card-side `libiomp5.so` for k1om, uOS, `mpss` service running, card `online` | host-side offload runtime (liboffload, COI), the card's `coi_daemon`, `mpss` running |
| `timing.transfer` | `null` | `null` (copying to the card is a deployment step, not timed) | measured (resident pattern) or included in kernel time (per-call, chunked) |
| failure without a card | n/a | the k1om ELF cannot execute on the host; on a non-k1om machine `knc_bench` exits 3 (`KNC_REQUIRE_MIC` uname check) | `_Offload_number_of_devices() == 0` → exit 3; every offload is `target(mic:0) mandatory`; code that ran without `__MIC__` → exit 4 |
| status | verified here | **UNVERIFIED** | **UNVERIFIED** |

Native and offload are **different execution modes with different runtime
requirements**. A working `ssh mic0` and native run do not prove that offload
works (it also needs COI/liboffload and matching compiler runtime versions), and
the reverse is also true. Results carry `execution_mode`. Never pool them.

## What is deliberately not here

- Hand-written IMCI intrinsics, KNC-specific tuning (prefetch pragmas, 2 MB
  pages, `#pragma simd`). They would go in a separate `mic/opt/` tree, so the
  portable code stays the like-for-like baseline.
- A host-only optimized variant (e.g. AVX-512 intrinsics or FFTW/MKL). Same
  rule: a separate directory and a separate record family.
- Neural networks on KNC (out of scope for Milestone 1).
