# MPSS 3.8 and KNC toolchain compatibility

Nothing in this file has been tried on the lab hardware. Every statement has
one of these labels:

- **SOURCED**: a historical source says so. The URL is given. The source was
  read as a search-engine excerpt, because this session's network blocked
  opening the pages directly; check the URL before relying on exact wording.
- **BELIEF**: widely repeated or follows from the sources, but has no direct quote.
- **UNVERIFIED**: no source found yet; must be confirmed on the lab machines.

Research notes behind this file were collected 2026-10-12.

## Toolchain

| item | status | detail / source |
|------|--------|-----------------|
| Last Intel C/C++ compiler with KNC (`-mmic` and offload) | SOURCED | **Intel C++ 17.0 (Parallel Studio XE 2017).** The 18.0 release notes under "Support Removed" say: "Support for the Intel Xeon Phi x100 product family coprocessor (formerly code name Knights Corner) is removed in this release." <https://www.intel.com/content/www/us/en/developer/articles/release-notes/c-compiler-legacy/18-0-linux-for-psxe2018.html>. The 2018 notes also say: "...announced end of life in January 2017 ... Intel Parallel Studio XE 2017 will be supported for a period of 3 years ending in January 2020". <https://www.intel.com/content/www/us/en/developer/articles/release-notes/visual-fortran-compiler-psxe/2018.html> |
| 18.0 rejects `-mmic` | SOURCED | forum report: <https://community.intel.com/t5/Software-Archive/Is-Intel-withdrawing-compiler-support-for-KNC/td-p/1166609> |
| Last MKL with KNC | SOURCED (search summary only) | MKL 2017. The MKL 2018 release notes carry the same removal entry: <https://www.intel.com/content/www/us/en/developer/articles/release-notes/intel-math-kernel-library-release-notes-and-new-features.html> |
| `-qopenmp` (not `-openmp`) | SOURCED | `-openmp` was deprecated in icc 15 (<https://cmake.org/pipermail/cmake-developers/2014-June/010753.html>) and rejected in 18.0.1 (<https://github.com/wrf-model/wrf/issues/694>). Our Makefiles use `-qopenmp`. |
| `-mmic` for native; no flag for offload | SOURCED | TACC/IXPUG lab: "to run natively on the MIC you must compile with -mmic. No options are required for offloading." <https://www.ixpug.org/docs/Offload_lab_2_instructions.pdf> |
| `-q[no-]offload[=none\|mandatory\|optional]` | SOURCED | C++ 16.0 guide: <https://portal.nacad.ufrj.br/online/intel/compiler_c/common/core/GUID-2077C232-DB36-4D54-9429-57F4660AB4BB.htm> |
| `-fp-model precise` with `-mmic` | UNVERIFIED | `-fp-model` is a general icc option; that it works with `-mmic` 17.0 is not confirmed. Needed for our NaN/Inf semantics (`docs/kernels.md`). |
| `__MIC__` is defined in `-mmic` and MIC-side offload compilation | BELIEF | Our native build `#error`s without it (`KNC_REQUIRE_MIC`), so a wrong belief fails at compile time, not silently. |
| `__INTEL_OFFLOAD` is defined when offload is enabled | BELIEF | `offload_magsq.c` `#error`s without it. |
| Where to get icc 17 / PSXE 2017 now | UNVERIFIED | Intel no longer distributes it publicly. Use existing licensed media or an institutional archive. Record the exact build (`icc -V`) in the results. |

## Offload semantics used by `mic/offload/offload_magsq.c`

| item | status | detail / source |
|------|--------|-----------------|
| Default offload mode in icc 15–17 is **mandatory** (error, no host fallback) | SOURCED | Intel forum: "the default offload mode is 'mandatory' and the app terminates with an error when no coprocessor is available". Fallback needs the `optional` clause or `-qoffload=optional`. <https://community.intel.com/t5/Software-Archive/running-offload-code-on-node-without-a-mic/m-p/1057882>. Older 13.x reports show "Host CPU (fallback mode)", so the default changed at some version (UNVERIFIED which). **We do not rely on the default:** every pragma says `mandatory`, we check the device count, and we check `__MIC__` inside the offload. |
| `target(mic:n)`: card n mod number of cards | SOURCED | <https://link.springer.com/chapter/10.1007/978-1-4302-5927-5_8>. With one card `mic:0` is the card. |
| `offload.h`: `_Offload_number_of_devices()` returns the number of working coprocessors or 0; `_Offload_get_device_number()` returns −1 on the host | SOURCED | <https://gnu.googlesource.com/gcc/+/refs/heads/devel/ranger/liboffloadmic/runtime/offload.h>, <https://community.intel.com/t5/Software-Archive/Targetting-specific-cards-at-runtime/m-p/985259> |
| `in`/`out`/`nocopy`, `length()`, `alloc_if()`/`free_if()`, `#pragma offload_transfer`, `__attribute__((target(mic)))`, the `mandatory` clause on a pragma | UNVERIFIED (no quote obtained) | Standard LEO syntax in the Intel C++ 15–17 guides and Jeffers & Reinders, *Intel Xeon Phi Coprocessor High-Performance Programming* (2013), ch. 7. Check against the 17.0 guide shipped with the compiler before the first build. |
| `OFFLOAD_REPORT=1/2` prints per-offload time / data traffic | SOURCED | <https://pds.ucdenver.edu/webclass/running%20Offload%20programs%20on%20Xeon-Phi.html> — use `OFFLOAD_REPORT=2` on the first run to confirm transfers happen where expected. |
| `MIC_ENV_PREFIX=MIC` forwards `MIC_*` variables (prefix stripped) to the card, e.g. `MIC_OMP_NUM_THREADS`, `MIC_KMP_AFFINITY` | SOURCED | same source; LSU slides <https://www.hpc.lsu.edu/static/training/weekly-materials/2015-Spring/mic_programming_1.pdf> |
| COI daemon on the card is required for offload | SOURCED (indirect) | Intel forum on affinity mentions the OS/COI daemon core: <https://community.intel.com/t5/Software-Archive/Thread-affinity-in-offload-codes/m-p/927880> |

## Threads and affinity

| item | status | detail / source |
|------|--------|-----------------|
| 5110P: 60 cores × 4 hardware threads = 240 | SOURCED | SKU: <https://www.intel.com/content/www/us/en/products/sku/71992/intel-xeon-phi-coprocessor-5110p-8gb-1-053-ghz-60-core/specifications.html> |
| Offload runtime keeps the last core (OS + COI daemon) free by default; suggested `MIC_KMP_PLACE_THREADS=59C,4t` (= 236 threads) | SOURCED | <https://community.intel.com/t5/Software-Archive/Thread-affinity-in-offload-codes/m-p/927880> |
| `KMP_PLACE_THREADS` renamed to `KMP_HW_SUBSET` (2016); syntax `30c,2t` | SOURCED | <https://reviews.llvm.org/D21340>. Which icc first accepts `KMP_HW_SUBSET` is UNVERIFIED (probably 17.0); `run_sweep.sh` uses `KMP_AFFINITY` and records both variables in every record. |
| `KMP_AFFINITY=balanced/compact/scatter` meanings on KNC | BELIEF | balanced: spread across cores like scatter, with consecutive thread IDs on the same core. compact: fill each core's 4 threads first. scatter: round-robin across cores. Confirm with `KMP_AFFINITY=verbose,balanced` on the card. |
| Native thread points | design | `run_sweep.sh` default: 1 2 4 8 16 30 60 120 236 240. 236 is included because of the OS core above. |

## MPSS 3.8.x

| item | status | detail / source |
|------|--------|-----------------|
| Last MPSS release: **3.8.6, 19 Sept 2019** | SOURCED | Internet Archive mirror of Intel's page, also a download source: <https://archive.org/details/intel-mpss-3.8.6> |
| 3.8.6 supported hosts: RHEL 6.7–6.9, **RHEL 7.1–7.3**, SLES 11 SP4, SLES 12 SP1/SP2 | SOURCED | same page. A conflicting claim says up to RHEL/CentOS 7.4 (kernel 3.10.0-693): <https://github.com/jjkeijser/mpss>. **Unresolved.** Plan on CentOS 7.3 (kernel 3.10.0-514) as the conservative choice; 7.4 needs testing. |
| 3.8.x after Q2 2017: only critical fixes, no new OS support | SOURCED | <https://community.intel.com/t5/Software-Archive/MPSS-3-8-5-is-released/m-p/1153873> |
| MPSS host kernel modules match the supported kernels; disable kernel updates; rebuild modules from the shipped source for other kernels | SOURCED for 3.4.1 (<http://pleiades.ucsc.edu/doc/intel/mic/mpss-3.4.1-readme.txt>); BELIEF for 3.8 | Read the 3.8.6 readme in the downloaded archive first. |
| Community-rebuilt modules for CentOS 7.5–7.9 and 8.x | SOURCED | <https://jjkeijser.github.io/mpss/>, <https://github.com/jjkeijser/mpss>; AlmaLinux 8.10 port: <https://github.com/ruribe17/Xeon-Phi-5110P-KNC-Drivers-Utilities-for-AlmaLinux-8.1>. Not Intel-supported; use only if 7.3 fails. |
| Supported-OS overview article | SOURCED | <https://www.intel.com/content/www/us/en/support/articles/000021566/processors/intel-xeon-phi-processors.html> |

## Card flash and SMC

| item | status | detail / source |
|------|--------|-----------------|
| `micflash -update` (no path) searches `/opt/intel/mic/flash` or `/usr/share/mpss/flash`; images like `EXT_HP2_B1_0386-03.rom.smc`; B1 stepping also needs `-smcbootloader` | SOURCED | <https://community.intel.com/t5/Software-Archive/SPI-flash-image-by-Flash-Update/td-p/962299> |
| Flash must match the installed MPSS; don't use an older release's `micflash` with a newer MPSS; very old flash (< 375-5) must go to 375-5 first | SOURCED | Intel Flash FAQ: <https://community.intel.com/legacyfs/online/drupal_files/Flash%20FAQ.pdf> |
| Flashing is an **operator step** | rule | No script in this repo runs `micflash`. Record `micinfo` before and after. |

## Guest diagnostics (run in the MPSS guest)

| command | what to look for | status |
|---------|------------------|--------|
| **[guest]** `lspci -nn \| grep -i 8086:2250` | the card is visible in the guest (PCI ID 8086:2250, subsystem 8086:2500) | SOURCED ID: <https://linux-hardware.org/?id=pci%3A8086-2250-8086-2500> |
| **[guest]** `service mpss start` (or the systemd unit; name UNVERIFIED) | service starts without errors | BELIEF |
| **[guest]** `micctrl -s` | `mic0: online` (`ready` = not booted) | SOURCED: <https://community.intel.com/t5/Software-Archive/HELP-Flash-Version-NotAvailable/m-p/954545> |
| **[guest]** `micinfo` | Flash Version, SMC Firmware Version, uOS Version, Device ID 0x2250; fields are incomplete until mpss is started | SOURCED (same) |
| **[guest]** `miccheck` | all tests pass | UNVERIFIED output format |
| **[guest]** `ssh mic0 uname -a` | the card's uOS answers; `uname -m` expected `k1om` | `k1om` is a BELIEF; `knc_bench` mic-native depends on it and fails loudly if it is wrong. Fix the check, not the result. |
| **[guest]** `micnativeloadex ./knc_bench -a "..."` with `SINK_LD_LIBRARY_PATH=<compiler>/lib/mic` | native run without ssh | SOURCED env var: <https://community.intel.com/t5/Software-Archive/micnativeloadex-problem/m-p/1009684> |

## Hardware

| item | status | source |
|------|--------|--------|
| 5110P: 60 cores, 1.053 GHz, 8 GB GDDR5, 320 GB/s, 1.011 TFLOPS FP64, 225 W, **passively cooled** | SOURCED | Intel SKU page above; <https://techpowerup.com/cpu-specs/xeon-phi-5110p.c2029> |
| BAR0: 64-bit prefetchable, 8 GB in a typical lspci; relocatable 256 MiB–32 GiB, set in flash | SOURCED | <https://community.intel.com/t5/Software-Archive/Intel-Xeon-Phi-5110P-device-is-not-online/td-p/1054907>, <https://community.intel.com/t5/Server-Products/Xeon-Phi-5110P-configuring-PCIe-MEMBAR0/m-p/1267603> |
| Host firmware must enable above-4G decoding / 64-bit MMIO | BELIEF (anecdotal forum advice; no Intel document found) | see `docs/qemu-passthrough.md` |
