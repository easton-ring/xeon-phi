# Public KNC Applications and Toolchain References

This note collects public examples and historical documentation relevant to the Intel Xeon Phi 5110P (Knights Corner / KNC). These links are leads for investigation, not evidence that any particular build works on this project's CentOS 7.3 / MPSS 3.8.6 environment.

## Start with public native examples

### ACCRE Intel Xeon Phi examples
- Repository: https://github.com/accre/Intel-Xeon-Phi
- Why it matters: a collection of Xeon Phi native/offload examples and build recipes. Inspect its `Native` and `Offload` areas, C sources, Makefiles, and application examples before inventing new test programs.
- How to use it: select a very small native example first; record its original compiler flags and assumptions; adapt only after confirming the target compiler and runtime are available.
- Caveat: repository age and the existence of example code do not guarantee compatibility with this machine, card, or MPSS release.

## Alternative native compiler investigation

### GCC cross-compiler shipped with MPSS (check this first)
- Location reported on Intel's forum: `/usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc`, with target libraries under `/usr/linux-k1om-4.7/linux-k1om/lib64` (sources in `docs/mpss-compatibility.md`).
- Why it matters: it may already be installed in the MPSS 3.8.6 guest. No build, no license.
- Caveat: reported to generate no code for the 512-bit vector unit. `mic/toolchain/probe-native.sh` finds it and reports K1OM, OpenMP, `__MIC__` and vector verdicts; `make -C mic/native TOOLCHAIN=gcc-k1om` builds the project with it.

### apc-llc GCC 5.1.1 adapted for KNC
- Repository: https://github.com/apc-llc/gcc-5.1.1-knc
- Why it matters: public GCC source and build notes aimed at the KNC `k1om` target. This is a potential route to investigating a native compiler without starting a compiler implementation from scratch.
- How to use it: review patches, target triplet, binutils requirements, runtime libraries, build prerequisites, and the documented deployment workflow. Keep all experiments in an isolated build prefix.
- Caveat: its notes refer to an older stack, including MPSS 3.6.1. Do not copy its commands verbatim into CentOS 7.3 / MPSS 3.8.6 without checking each assumption.

### Historical GCC OpenMP offload discussion
- Article/thread: https://community.intel.com/t5/Software-Archive/GCC-OpenMP-4-0-Offloading-to-a-Real-Knights-Corner-Xeon-Phi-Card/td-p/1064161
- Why it matters: historical discussion of a patched GCC 5.1.0 setup and a vector-multiply test on real KNC hardware. It may help distinguish native compilation from host-driven offload.
- Caveat: this is historical community material tied to an older toolchain/MPSS generation; it is not a supported-toolchain guarantee for MPSS 3.8.6.

## Intel historical programming material

### Intel hStreams introduction and sample
- Article: https://www.intel.com/content/www/us/en/developer/articles/technical/introduction-to-heterogeneous-streams-library.html
- Why it matters: historical host/device example and MIC-side compilation/runtime context.
- How to use it: treat as a reference for architecture and API usage, then check whether the relevant headers, libraries, and runtime components are actually present in the installed MPSS environment.
- Caveat: documentation or source availability does not imply that the necessary Intel SDK/compiler components are installed or redistributable.

### Intel Community compatibility discussions
- Starting point: https://community.intel.com/
- Why it matters: useful place to locate legacy KNC FAQs and discussions about target compatibility and compiler requirements.
- Caveat: this is a broad portal rather than a specific citation. If project documentation makes a compatibility claim based on a forum post, replace this general link with the exact thread and capture its relevant version details.

## Supercomputing context

### TOP500
- Archive: https://www.top500.org/
- Why it matters: source for historical system listings and context on Xeon Phi in large HPC deployments.
- How to use it: cite a specific system entry or a primary institutional/system source when documenting a named supercomputer such as Tianhe-2. Do not use the TOP500 landing page alone as evidence for detailed workload claims.

## Technical constraints to keep in view

- Knights Corner uses the K1OM target ABI and ISA; an ordinary x86-64 executable should not be assumed to run natively on KNC.
- “Compiler accepted the source” is not sufficient. Verify object/executable metadata, link against the intended target libraries, deploy to the card, and run a small deterministic test on the physical device.
- Native execution and host-driven offload are distinct paths. Record which path each example uses.
- Record compiler version, target triple, MPSS version, linker/binutils version, runtime dependencies, full commands, and logs for every successful probe.
- Do not claim hardware support until validated on the physical Xeon Phi 5110P.

## Suggested investigation sequence

1. Run `mic/toolchain/collect-toolchain.sh` and `mic/toolchain/probe-native.sh` from PR #6 in the CentOS 7.3 VM.
2. Inspect the GCC KNC fork's prerequisites and compare them with the inventory results.
3. Try the smallest native C example in an isolated prefix; avoid modifying the system compiler, kernel, or MPSS installation during reconnaissance.
4. Confirm K1OM output with target-aware binutils/readelf tools, then link against the correct target runtime.
5. Copy and execute the test on the physical card and compare deterministic output against a host reference.
6. Only after that, evaluate whether project kernels can be built and run through a native or offload path.

## Related project work

- [Toolchain inventory/probe PR #6](https://github.com/easton-ring/xeon-phi/pull/6)
- [Alternative native compiler investigation issue #5](https://github.com/easton-ring/xeon-phi/issues/5)
- [Build/offload audit](https://github.com/easton-ring/xeon-phi/blob/main/docs/knc-build-offload-audit.md)
- [Reference tracking issue #7](https://github.com/easton-ring/xeon-phi/issues/7)
