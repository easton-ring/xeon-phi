# MPSS 3.8 and KNC toolchain compatibility

Nothing in this file has been tried on the lab hardware. Every statement has
one of these labels:

- **SOURCED**: a historical source says so. The URL is given. The source was
  read as a search-engine excerpt, because this session's network blocked
  opening the pages directly; check the URL before relying on exact wording.
- **PRIMARY**: read in full from an Intel document (text supplied by the
  project owner from a Wayback Machine capture); see "Native build article".
- **BELIEF**: widely repeated or follows from the sources, but has no direct quote.
- **UNVERIFIED**: no source found yet; must be confirmed on the lab machines.

Research notes behind this file were collected 2026-10-12.

## Native build article (PRIMARY)

Intel, A. K. Sharp, *Building a Native Application for Intel® Xeon Phi™
Coprocessors*, published 2014-03-27, updated 2014-09-09.
<https://software.intel.com/en-us/articles/building-a-native-application-for-intel-xeon-phi-coprocessors>
(offline; the text was read from a Wayback Machine capture, 7 captures between
2020-08-08 and 2021-06-19). It was written for **Composer XE 2013 SP1
(icc 14.0)**, so paths below are 2013-era; differences for icc 17 are noted.
What this project takes from it:

| topic | what the article says | effect here |
|-------|-----------------------|-------------|
| compiler env | `source /opt/intel/composer_xe_2013_sp1/bin/compilervars.sh intel64`, check with `icc -V` | icc 17 installs under a different prefix (BELIEF: `/opt/intel/compilers_and_libraries_2017/linux/bin/compilervars.sh`); record `icc -V` in results |
| native flag | Linux `-mmic`, Windows `/Qmic`; code "runs only on Intel Xeon Phi coprocessors" | `mic/native/Makefile` uses `-mmic` |
| optimization | default is `-O2` when no `-On` is given | we pass `-O3` explicitly; the host build also uses `-O3` |
| native libraries | build them with `-mmic` (`-fpic -shared` for .so, `xiar crs` for .a) | not needed: the Makefile compiles the C core sources straight into each tool |
| runtime dependencies | must be copied to the card. Default dir `/opt/intel/composer_xe_2013_sp1.X.XXX/compiler/lib/mic/`. Typical C set: `libiomp5.so`, `libimf.so`, `libsvml.so`, `libintlc.so.5`, `libirng.so` (plus Fortran, Cilk and MPI libs if used). glibc libs are already on the card | **`deploy.sh` previously copied only `libiomp5.so`**; now copies the full set from `MIC_LIBDIR`. icc 2017 dir: `…/compiler/lib/intel64_lin_mic` (forum report, below) |
| dependency listing | `SINK_LD_LIBRARY_PATH=<mic libdir> /opt/intel/mic/bin/micnativeloadex a.out -l`; prints "Binary was built for … Knights Corner" plus found and not-found deps | `deploy.sh` logs this to `build-mic/knc_bench.deps.txt` |
| transfer | MPSS networking gives `mic0`, `mic1`, …; `ssh mic0 'mkdir /tmp/x'`, `scp a.out libiomp5.so mic0:/tmp/x`. Non-root ssh/scp needs superuser setup | matches `deploy.sh` |
| card filesystem | local FS is a **RAM disk in GDDR5**: files there consume the memory the application needs. For large data, NFS-mount a host directory (host `/etc/exports` entry for the card's private IP, default range e.g. `172.31.1.1`; card `/etc/fstab` entry) | dev dataset is small (13 MB per split) and goes to `/tmp`; NFS is an operator step, not scripted (it edits host config) |
| running manually | `ssh mic0`, `cd`, `chmod +x`, `export LD_LIBRARY_PATH=<dir>:$LD_LIBRARY_PATH`, `./a.out` | `run_sweep.sh` does this per run |
| micnativeloadex | copies binary + deps, runs, redirects output; "**not intended for use when measuring application performance**" | benchmarks use ssh only; micnativeloadex just for smoke tests |
| debugging | GDB for MIC; IDB deprecated in 2013 SP1 | not used |

Not in the article (still covered by the rows below): `uname -m`, thread
affinity, `-qopenmp` spelling, offload, MPSS versions.

## Toolchain

| item | status | detail / source |
|------|--------|-----------------|
| Last Intel C/C++ compiler with KNC (`-mmic` and offload) | SOURCED | **Intel C++ 17.0 (Parallel Studio XE 2017).** The 18.0 release notes under "Support Removed" say: "Support for the Intel Xeon Phi x100 product family coprocessor (formerly code name Knights Corner) is removed in this release." <https://www.intel.com/content/www/us/en/developer/articles/release-notes/c-compiler-legacy/18-0-linux-for-psxe2018.html>. The 2018 notes also say: "...announced end of life in January 2017 ... Intel Parallel Studio XE 2017 will be supported for a period of 3 years ending in January 2020". <https://www.intel.com/content/www/us/en/developer/articles/release-notes/visual-fortran-compiler-psxe/2018.html> |
| 18.0 rejects `-mmic` | SOURCED | forum report: <https://community.intel.com/t5/Software-Archive/Is-Intel-withdrawing-compiler-support-for-KNC/td-p/1166609> |
| Last MKL with KNC | SOURCED (search summary only) | MKL 2017. The MKL 2018 release notes carry the same removal entry: <https://www.intel.com/content/www/us/en/developer/articles/release-notes/intel-math-kernel-library-release-notes-and-new-features.html> |
| `-qopenmp` (not `-openmp`) | SOURCED | `-openmp` was deprecated in icc 15 (<https://cmake.org/pipermail/cmake-developers/2014-June/010753.html>) and rejected in 18.0.1 (<https://github.com/wrf-model/wrf/issues/694>). Our Makefiles use `-qopenmp`. |
| `-mmic` for native; no flag for offload | PRIMARY (`-mmic`), SOURCED (offload) | Linux spelling is the hyphen form, e.g. `ifort -mmic -qopenmp program.f95`; Intel's "Building a Native Application for Intel Xeon Phi Coprocessors" article mixes Linux and Windows option styles (forum note: <https://community.intel.com/t5/Software-Archive/Binary-error-on-Xeon-Phi/m-p/1062807>). The article itself (<https://software.intel.com/en-us/articles/building-a-native-application-for-intel-xeon-phi-coprocessors>) could not be opened from the authoring environment. |
| native vs offload build flags | SOURCED | TACC/IXPUG lab: "to run natively on the MIC you must compile with -mmic. No options are required for offloading." <https://www.ixpug.org/docs/Offload_lab_2_instructions.pdf> |
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
| **[guest]** `ssh mic0 uname -a` | the card's uOS answers; machine string `k1om` | SOURCED (indirect): an Intel forum FAQ thread shows `uname -a` from a card (`…-mic0`) ending in `k1om` (<https://community.intel.com/t5/Software-Archive/FAQ/m-p/816368>). `uname -m` = `k1om` is inferred from that, not quoted. `knc_bench` mic-native depends on it and fails loudly if it is wrong; fix the check, not the result. |
| **[guest]** `micnativeloadex ./knc_bench -a "..."` with `SINK_LD_LIBRARY_PATH=<MIC lib dir>` | native run without ssh; micnativeloadex copies the binary **and its library dependencies** to the card, but not data files, and does not forward stdin | SOURCED: <https://community.intel.com/t5/Software-Archive/micnativeloadex-problem/m-p/1009684>, <https://www.linux.com/?p=487507> (stdin). Missing `libiomp5.so` on the card is the usual failure; the fix is `SINK_LD_LIBRARY_PATH` (or `LD_LIBRARY_PATH` with ssh) pointing at the **MIC** build of the compiler libraries (<https://csc.cnsi.ucsb.edu/docs/using-xeon-phi-coprocessor>). |
| MIC library directory for **icc 2017** | `…/compiler/lib/intel64_lin_mic` (older Composer XE releases: `…/compiler/lib/mic`) | SOURCED: a user's fix with the 2017 compiler was pointing the path at `compiler/lib/intel64_lin_mic`: <https://community.intel.com/t5/Software-Archive/Binary-error-on-Xeon-Phi/m-p/1062819>. Confirm with `find /opt/intel -name libiomp5.so -path '*mic*'`. |

## Community driver ports (fallbacks if the 7.3 guest fails)

**Why the modern host kernel doesn't need MPSS here.** In this project's
design the card is passed through with VFIO. The lab host's kernel (e.g. 6.x)
only binds `vfio-pci` and never loads the MIC driver. MPSS runs in an EL7 guest
on a kernel it supports. So porting the `mic` driver to a 6.x kernel is **not**
on the Milestone 2 path. These ports matter only if the 7.3 guest fails (for
example MPSS 3.8.6 not matching the guest kernel) and a newer guest kernel is
needed.

Statuses were checked 2026-10-12, using search excerpts only.

| project | claim | status |
|---------|-------|--------|
| Jan Just Keijser (Nikhef), `jjkeijser/mpss` | Patched MPSS 3.8.6 `mpss-modules` for newer RHEL/CentOS kernels, with RPMs, SRPMs and build notes. KNC. | SOURCED: page "Intel Xeon Phi (KNC) MPSS software and kernel modules for CentOS 7&8" <https://www.nikhef.nl/~janjust/mpss>, <https://jjkeijser.github.io/mpss/>, <https://github.com/jjkeijser/mpss>. RPM posts for CentOS 7.5, 7.6 and 7.8: <https://community.intel.com/t5/Software-Archive/MPSS-3-8-6-modules-for-CentOS-7-8/m-p/1173726>. "Use at your own risk; tested on my CentOS 7.8 box." Coverage up to CentOS 8.5 (kernel 4.18) was claimed in an earlier summary but not confirmed by a quote. |
| `ruribe17`: KNC 5110P drivers and utilities for AlmaLinux | Port based on MPSS 3.8.6 | SOURCED for **AlmaLinux 8.10** (needs Python 2 fixes in mpss-micmgmt/miccheck): <https://github.com/ruribe17/Xeon-Phi-5110P-KNC-Drivers-Utilities-for-AlmaLinux-8.1>. An **AlmaLinux 9 / kernel 5.14** variant (said to be updated Feb 2025) was claimed in an earlier summary; **not found** in search, so UNVERIFIED. |
| `devcapcap/XeonPhi` | MPSS **4.4.1** modules on kernel 5.6-rc1 / Ubuntu 20.04, with freezes on some cards | **UNVERIFIED** (not found). Even if accurate, MPSS 4.x is the **KNL** (x200) stack; it does not drive a 5110P (KNC, x100). An Intel forum reply in the MPSS-modules threads says KNL PCIe cards need MPSS 4.4, which "is not publicly available" and would need porting to kernel 4.18+ (search excerpt; exact thread not pinned, see <https://community.intel.com/t5/Software-Archive/MPSS-3-8-6-modules-for-CentOS-7-8/m-p/1173726>). Not usable here (brief §1.1: KNC is not KNL). |
| `subgeniuskitty`: writeup and module port based on MPSS 3.8.6 | | **UNVERIFIED** (not found in search). |
| Gentoo ebuild (older) | `mic` module ported to Linux 3.17.8 with small edits | SOURCED: <https://community.intel.com/t5/Software-Archive/Xeon-Phi-and-Gentoo/m-p/1051377> |
| xdsopl `mpss-modules` | a GitHub copy of the modules | SOURCED (exists): <https://github-redirect.dependabot.com/xdsopl/mpss-modules>; contents not reviewed |

**Fallback plan if the CentOS 7.3 guest fails** (UNVERIFIED):

1. Use a CentOS **7.9** guest (kernel 3.10.0-1160) with Keijser's patched
   3.8.6 modules (his CentOS 7 RPM/SRPM). That keeps the MPSS 3.8.6
   userspace and the EL7 toolchain, and only swaps the kernel modules.
2. Change `mic/guest/centos73-mpss.ks` to point at the 7.9 vault tree, and run
   `install-mpss-guest.sh` with
   `EXPECT_KERNEL=3.10.0-1160.el7.x86_64` after installing the patched
   `mpss-modules` RPM matching that kernel.
3. Record which module build (URL plus SHA-256 of the RPM) was used in
   `results/hw/<date>/diag/`. Results from it count as unofficial-driver
   results.

A forum reply on Keijser's RPMs blamed a card that "wouldn't be used" on the
BIOS **"Above 4G decoding"** setting
(<https://community.intel.com/t5/Software-Archive/MPSS-3-8-4-modules-for-CentOS-7-5/m-p/1151706>).
This is a second anecdotal source for that firmware setting.

## Hardware

| item | status | source |
|------|--------|--------|
| 5110P: 60 cores, 1.053 GHz, 8 GB GDDR5, 320 GB/s, 1.011 TFLOPS FP64, 225 W, **passively cooled** | SOURCED | Intel SKU page above; <https://techpowerup.com/cpu-specs/xeon-phi-5110p.c2029> |
| BAR0: 64-bit prefetchable, 8 GB in a typical lspci; relocatable 256 MiB–32 GiB, set in flash | SOURCED | <https://community.intel.com/t5/Software-Archive/Intel-Xeon-Phi-5110P-device-is-not-online/td-p/1054907>, <https://community.intel.com/t5/Server-Products/Xeon-Phi-5110P-configuring-PCIe-MEMBAR0/m-p/1267603> |
| Host firmware must enable above-4G decoding / 64-bit MMIO | BELIEF (two anecdotal forum reports, see "Community driver ports"; no Intel document found) | see `docs/qemu-passthrough.md` |
