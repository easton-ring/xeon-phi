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

**D-11 Cross-language tests use ctypes plus subprocess.** CMake also builds
`libknc.so` from the same object files as the static library. pytest calls
kernels and features through ctypes (`knc_rf_lab.cbin`) and calls the CLI
tools (`knc_validate`, `knc_bench`, `knc_infer`) as subprocesses. `cbin`
builds the tree on demand, so a plain `pytest` works.

**D-12 `knc_bench` is self-describing.** Compiler, flags, git commit (baked in
at configure time; `make` reconfigures every run), CPU model, hypervisor flag,
OS and dataset SHA-256 (a C SHA-256 implementation) are written by the binary
itself. Reason: on the card there is no Python to add metadata afterwards.

**D-13 Features are computed in double from float32 input**, in both C and
NumPy. Reason: cumulants involve cancellation; double makes 1e-9 agreement
possible and keeps the classifier comparison about the model, not rounding.
The DSP kernels have both precisions.

**D-14 `-ffp-contract=off -fno-fast-math`** on the host build, and
`-fp-model precise` in the KNC Makefiles (icc defaults to value-unsafe
`fast=1`). Reason: NaN/Inf propagation tests and reproducible rounding.

**D-15 TorchSig lives in a separate venv.** The CPU-only torch index is
blocked, so the only installable torch pulls about 3 GB of CUDA wheels. The
adapter is tested there (`docs/torchsig.md`) and skipped with a reason in the
dev venv.

**D-16 Fail-loud MIC guards are layered.** Native builds: compile-time
`#error` without `__MIC__`, plus a runtime `uname -m` check. Offload: device
count check, the `mandatory` clause on every pragma, and an in-region
`__MIC__` probe that aborts if code ran on the host. Reason: the default
offload mode differs between icc versions (`docs/mpss-compatibility.md`).

**D-17 Periodic Hann window**, not `numpy.hanning` (symmetric), and the
periodogram is normalized by Σw² (`docs/kernels.md`).

**D-18 Example outputs committed in `results/examples/`**: compact benchmark
JSON (~200 KB), plot, evaluation metrics, confusion matrices and run logs from
the final validation run. Generated datasets and models are not committed.
They regenerate in seconds and their identity is the SHA-256.

**D-19 MPSS guest VM as files, not automation of the host.** `mic/guest/`
contains a kickstart (CentOS 7.3.1611 from the vault, kernel locked at
3.10.0-514), a libvirt domain with `hostdev managed='no'`, and guest-side
scripts. `managed='no'` is deliberate: with `managed='yes'` libvirt would
detach the card from its host driver and bind vfio-pci automatically, which
the brief (§1.6) reserves for the operator. The install script never flashes.
EL7.3 lacks CMake ≥ 3.13 and Python ≥ 3.10, so the guest gets a plain-gcc
`make -C mic/guest check` instead of `make test`.

**D-20 `CC` defaults in the MIC Makefiles.** GNU make predefines `CC=cc`, so
`CC ?= icc` never applied (a silent non-icc build). The Makefiles now set the
compiler only when `$(origin CC)` is `default`.

**D-21 Native deployment copies the full Intel MIC runtime set.** Intel's
native-build article (PRIMARY in `docs/mpss-compatibility.md`) lists
`libiomp5.so libimf.so libsvml.so libintlc.so.5 libirng.so` as the typical C
runtime dependencies of `icc -mmic` binaries. `deploy.sh` takes the directory
(`MIC_LIBDIR`) instead of one `libiomp5.so` path, copies that set, and logs
`micnativeloadex -l`. Benchmarks keep running over ssh, because the article
says micnativeloadex is not meant for performance measurement.

**D-22 No host-kernel driver port.** Community MPSS ports (Keijser for
CentOS 7/8; an AlmaLinux 8.10 port; unconfirmed reports of 5.x-kernel ports)
are recorded in `docs/mpss-compatibility.md` as fallbacks only. With VFIO
passthrough the lab host kernel never loads the `mic` driver, so porting it
to a 6.x kernel is not on the Milestone 2 path. The first fallback is a
CentOS 7.9 guest with Keijser's patched 3.8.6 modules. KNL-stack ports
(MPSS 4.x) are excluded: they do not drive the KNC 5110P. devcapcap/XeonPhi (read in full) has no booted card on kernel 5.6, and its KNC attempts with ported 3.x modules froze the host.

**D-23 Driver-port route.** If the KNC host driver is ever ported to a modern
kernel (`docs/kernel-port.md`), forward-port Keijser's patched MPSS 3.8.6
modules (route A) and use mainline v5.9 `drivers/misc/mic` only as a
reference. Reason: mainline's stack needs its own card-side kernel, and
mainline has no KNC CPU build target, while route A keeps Intel's card OS and
userspace. Development happens in a passthrough dev guest; `mic/port/ladder.sh`
refuses to run on bare metal by default.

**D-24 No Intel compiler: native via the MPSS GCC cross-compiler, offload
blocked.** Issue #5: no icc installer or license. `mic/native/Makefile` gains a
`TOOLCHAIN=gcc-k1om` profile for the k1om GCC that MPSS reportedly ships. It
may not define `__MIC__`, so that profile replaces the compile-time guard with a
mandatory post-link check that every binary is a K1OM ELF (both profiles run
it). That compiler reportedly does not use the 512-bit vector unit, so records
carry `metadata.toolchain` and gcc-k1om results are reported as scalar KNC
results. LEO offload stays icc-only and is blocked until icc is available.

**D-25 K1OM verdicts come from the ELF header.** `mic/toolchain/elf-target.sh`
reads `e_machine` (181 = EM_K1OM) with `od`. The earlier probe grepped `file`
and `objdump` output, which includes the file name; names containing "k1om"
made a host object read as K1OM. The probe, the native Makefile and
`deploy.sh` all use the header check; `selftest.sh` guards against regressions.

**D-26 Lab host: dual Xeon Platinum 8175M on a Gigabyte MD71-HB0.** One
machine serves both as the bare-metal host baseline and as the passthrough
host for the MPSS VM. The 8175M is an OEM part (TDC 255 A, TDP 240 W); retail
listings rate the board at 205 W per socket. The full ServeTheHome ICC_MAX
thread (63 pages, read end to end) reports that the MD71-HB0 ships with
ICC_MAX = 255 A and "usualy runs dual Platinum 8175M", so the build plans for
**no VRM modification**. The open risks are used-CPU condition, socket
installation, cooling of two in-line 240 W CPUs and their VRMs, and power
limits applied by BIOS or BMC (`docs/host-build.md` §1a). If the mod is ever
needed, the VRM NVM write (and the scan, which needs extra hardware on the
board) stays an operator procedure, like card flashing, and is never scripted
in this repository. `mic/host/host-check.sh` (read-only) records the RAPL
package limits and microcode, together with the passthrough prerequisites.
The board's original Xeon Silver 4214s are kept as known-good CPUs: the board,
cards and IOMMU are proven on them before the swap (`docs/host-build.md` §1c);
no host baseline is taken on them. Bring-up runs in four stages, each ending
with something working: board on the 4214s, the 8175Ms, the GPU, then the Phi
(§1c). Memory: 12 DIMMs, CPU0 six identical 8 GB for the baseline, CPU1 mixed
(§2b). Chassis: an open steel bench with ducted fans on the passive cards
(§2c); PSU: Corsair HX1500i. An optional NVIDIA GPU is reported by the
host check (never a FAIL) and sized into a 1600 W PSU budget (§2a); GPU
results, if any, are a separate record family.
