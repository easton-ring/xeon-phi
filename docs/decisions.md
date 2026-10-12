# Engineering decisions

Routine decisions made while building Milestone 1. Each entry records what was
decided and why. Decisions taken over from the brief unchanged are not listed
except where a detail had to be chosen.

## Phase 1 — environment inspection (2026-10-12)

Container (a cloud VM, **not** the lab host):

| item | found |
|------|-------|
| CPU | `Intel(R) Xeon(R) Processor @ 2.10GHz`, family 6 model 207 (Emerald Rapids class), 4 vCPU, KVM guest, AVX-512 |
| RAM / disk | 15 GiB / ~31 GiB free |
| Python | 3.13.16 (system); venv in `.venv/` |
| NumPy / PyYAML | 2.5.4 / 6.0.x in the venv (system had NumPy 2.5.3) |
| pip extras | scikit-learn 1.9.1, matplotlib 3.11.2, pytest 9.1.1 installed from PyPI |
| C toolchain | gcc/g++ 13.3.0 (Ubuntu 24.04), CMake 3.28.3, GNU make |
| Not present | icc/icpc, MPSS, KVM device, Xeon Phi |
| TorchSig | see `docs/torchsig.md` |

`download.pytorch.org` (CPU-only wheel index) is blocked by the proxy (HTTP 403
on CONNECT), so the only installable torch is the PyPI build with CUDA
dependencies.

### Plan

Phases as in the brief §10: skeleton → C core → benchmark → classifier →
demo → MIC prep → validate. One end-to-end path first, depth later.

## Decisions

**D-01 Branch.** The session harness assigns branch
`claude/zen-hawking-0v2ik6`; the brief names `claude/xeon-phi-rf-dsp-l8qh3t`
(already merged to `main` as PR #1). All work is pushed to the
harness-assigned branch.

**D-02 Python layout.** `python/knc_rf_lab/` with a setuptools `src`-style
`packages.find(where="python")`. Editable install via `pip install -e ".[dev]"`.

**D-03 No `jsonschema` dependency.** Benchmark records are validated against a
JSON Schema document (`python/knc_rf_lab/schemas/benchmark.schema.json`) by a
~60-line validator that implements the subset of draft 2020-12 the schema uses
(`type`, `required`, `properties`, `additionalProperties`, `enum`, `const`,
`items`, `minimum`, `pattern`, `oneOf`). Reason: core deps are numpy+pyyaml only,
and `jsonschema` drags in compiled `rpds-py`.

**D-04 Generator RNG.** `SeedSequence(entropy=master_seed,
spawn_key=(crc32(split),))`, one spawned child per example plus one for class
order; PCG64. Gives disjoint, order-independent streams per split and example.

**D-05 Generated dataset identity.** Byte-identity is guaranteed only for a
fixed NumPy version and CPU family (see `docs/dataset-format.md`,
"Reproducibility"). Results carry the dataset SHA-256.

**D-06 SNR definition.** Signal normalized to unit power *before* fading, so
`snr_db` is the average SNR and Rayleigh draws change the instantaneous SNR.
The fading gain is recorded per example.

**D-07 Header CRC covers labels.** Field 56 renamed `payload_crc32` →
`data_crc32` and covers labels + payload. Reason: the brief's layout left the
labels unprotected; a bit flip in a label would silently change training data.

**D-08 Impairments always in CSV.** Per-example parameters are always written
to `<split>.impairments.csv` (with its SHA-256 in the sidecar) instead of
switching between inline JSON and a side file by size. One code path.

**D-09 Error codes.** Python and C readers share symbolic error codes
(`E_MAGIC`, …) and check in the same order, so cross-language tests compare
codes, not messages.

**D-10 OFDM class included.** 8 classes: bpsk, qpsk, 8psk, 16qam, 64qam, 2fsk,
4fsk, ofdm.
