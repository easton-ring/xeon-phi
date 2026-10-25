# KNC native toolchain inventory and probes

These scripts investigate whether the current CentOS 7.3 / MPSS 3.8.6 guest has a compiler capable of producing **native Knights Corner (K1OM) machine code**. They do not install packages, change kernel modules, configure MPSS, flash firmware, or bind devices.

Run from the repository root inside the guest:

```sh
sh mic/toolchain/collect-toolchain.sh
sh mic/toolchain/probe-native.sh
```

Both scripts print their output directory. By default, logs are written below `results/toolchain/<UTC timestamp>/`. You can select an output directory:

```sh
sh mic/toolchain/collect-toolchain.sh results/toolchain/inventory-run1
sh mic/toolchain/probe-native.sh results/toolchain/probe-run1
```

Collect diagnostics first, then run the probes. Share the resulting text files and `SUMMARY.txt` in an issue comment or attach them to issue #5.

## What the scripts check

- OS release, kernel, architecture, CPU and memory information.
- GCC, G++, Clang, LLVM target listing, assembler, linker, `file`, `readelf`, and `objdump` availability and versions.
- Compiler environment hints, without dumping the complete environment (which can contain secrets).
- MPSS package versions, card state, card identity, and SSH reachability when the corresponding tools are installed.
- Intel MIC/KNC runtime library paths beneath `/opt/intel` and standard library locations.
- A host-only C compilation as a control.
- An Intel compiler `icc -mmic` object-code probe, a GCC `-mmic` probe, and a Clang `--target=k1om-linux-gnu` probe when those tools exist.
- **The k1om GCC cross-compiler shipped with MPSS** (forum-sourced location `/usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc`; it is not on `PATH`). The inventory searches `/usr/linux-k1om-*`, `/opt/mpss` and `PATH` for `*k1om*-gcc`; the probe then, for each candidate (or `KNC_CROSS_CC=/path/to/compiler`):
  - builds an object **and** links a full executable (never executed),
  - links an OpenMP program and lists its `NEEDED` libraries (is there a k1om `libgomp`?),
  - dumps the predefined macros (is `__MIC__` defined? see `*-macros.txt`),
  - compiles a vector loop to assembly and reports `VECTOR_VERDICT: ZMM_USED` or `NO_ZMM` (whether the 512-bit vector unit is used at all).
- Object metadata. The K1OM verdict comes from the ELF header's `e_machine` field (`elf-target.sh`, EM_K1OM = 181), not from tool output: `file` and `objdump` print the file name, and probe names contain "k1om", which used to produce false positives.

## Interpret the result conservatively

A host compilation succeeding proves only that the ordinary host compiler works. It does **not** prove KNC support.

The strongest positive result from `probe-native.sh` is an executable whose ELF machine is positively identified as **Intel K1OM** (the KNC target). If compilation succeeds but the machine type cannot be identified, the script labels the result inconclusive. A compiler accepting an option is not enough.

A linked K1OM executable is still not a tested one: it must be copied to the card with its target libraries and run on the physical 5110P (`mic/native/deploy.sh` does that, with a smoke run). The probes stop before running anything.

`NO_ZMM` for the MPSS cross-compiler means binaries built with it (`make -C mic/native TOOLCHAIN=gcc-k1om`) are scalar KNC code. That is a valid but different experiment from icc-vectorized code; benchmark records carry `metadata.toolchain` so the two are never mixed.

The GCC `-mmic` and Clang target probes are exploratory negative/positive checks, not assertions that those projects support KNC. Modern compiler releases may have removed old targets. Do not install a random old compiler or alter the VM based on one failed probe.

## Optional custom compiler probe

If you have a candidate compiler and know its target flags, you can try it without editing the script:

```sh
KNC_CC=/path/to/compiler KNC_CFLAGS='-target k1om-linux-gnu' \
  sh mic/toolchain/probe-native.sh results/toolchain/custom-probe
```

The command is run as a compiler path plus whitespace-separated flags. Use only a trusted local compiler path and ordinary compiler flags. The result is still considered positive only when object metadata identifies K1OM.

## Self-test

`sh mic/toolchain/selftest.sh [WORKDIR]` checks the verdict logic on any machine with gcc: a forged K1OM object must be recognised, a host compiler disguised as a k1om cross-compiler (in a path containing "k1om") must never be reported as K1OM, and the inventory must list every tool. CI (`.github/workflows/knc-toolchain-probes.yml`) runs it plus `shellcheck`, and fails on a regression.

## Safety and limitations

- These scripts are read-only with respect to system configuration; they create result files and temporary probe artifacts only.
- Card status and SSH checks may fail if the card is offline or SSH is not configured; those failures are recorded and do not stop the inventory.
- The probe can run without root privileges.
- Nothing in these scripts validates Intel offload pragmas, the offload runtime, or MIC-side OpenMP behavior.
- CentOS 7 and MPSS 3.8 are obsolete; do not expose this guest to untrusted networks while investigating legacy tooling.

See also: [MPSS compatibility notes](../../docs/mpss-compatibility.md), [native build/offload audit](../../docs/knc-build-offload-audit.md), and [issue #5](https://github.com/easton-ring/xeon-phi/issues/5).
