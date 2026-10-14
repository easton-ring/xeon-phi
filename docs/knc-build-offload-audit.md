# KNC Native Build and Offload Audit

**Status:** review recommendations; hardware validation pending  
**Scope:** KNC-native build/deployment and Intel MIC offload path  
**Target:** Intel Xeon Phi 5110P (Knights Corner, KNC)

This document records risks and verification work identified during a source/documentation review. It is **not** a report of confirmed hardware failures. The KNC-native and offload paths have not yet been proven by a real build and run on the target toolchain and card. Keep that distinction in benchmark reports.

## Summary

The project has a sensible separation between the portable host pipeline, KNC-native binaries, and an offload experiment. Before treating KNC results as valid, close the following gaps:

1. Verify compilation and execution with the actual Intel KNC-capable compiler and MPSS runtime.
2. Verify offload pragma, pointer, buffer-lifetime, and transfer behavior with a real offload build.
3. Make MIC-side OpenMP thread configuration explicit and record requested versus observed values.
4. Validate native runtime dependencies instead of relying on a best-effort library copy.
5. Harden size/argument arithmetic and boundary tests.
6. Make sweep failures and build provenance unambiguous.

Priority labels are engineering priorities, not claims that a defect has been reproduced.

## Findings and recommendations

### P1 — Prove MIC-side thread configuration

The offload experiment records `omp_get_max_threads()`, but a recorded value alone does not prove that the requested thread count was applied to the MIC-side region. Setting `OMP_NUM_THREADS` in the host-side sweep is not sufficient evidence of the coprocessor runtime's environment.

**Recommended actions**
- Add an explicit thread-count option to the offload benchmark if feasible.
- Set the MIC-side OpenMP environment using the mechanism supported by the installed Intel runtime (for example, verify the behavior of `MIC_ENV_PREFIX=MIC` and the corresponding `MIC_OMP_NUM_THREADS` variable for this exact version).
- Record requested and observed thread counts with each result.
- On hardware, use runtime diagnostics such as `KMP_AFFINITY=verbose` to verify placement and team size.
- Do not label a sweep as a thread-scaling experiment until the settings are confirmed on the MIC.

### P1 — Compile offload code with the real Intel compiler

A GCC syntax-check with stub headers and unknown pragmas suppressed can detect ordinary C syntax errors, but it cannot validate Intel offload pragmas, data-transfer clauses, or device compilation. Compile-time guards such as `__INTEL_OFFLOAD` also need confirmation against the actual supported compiler version.

**Recommended actions**
- Build the offload target using the intended Intel compiler installation and its KNC offload support.
- Save compiler version output and the full build log as experiment artifacts.
- Inspect the compiler's offload report and verify that the kernel is emitted for the MIC rather than silently running only on the host.
- Keep the GCC syntax-check, but describe it as a limited syntax check only.

### P1 — Validate all offload patterns end to end

The resident-buffer pattern depends on the exact behavior of allocation, free, and no-copy clauses, pointer mapping, and data lifetime. These are not proven by a host-only syntax check.

**Recommended actions**
- Use a small deterministic input and compare every output element against an independent host reference.
- Test all supported patterns repeatedly: resident buffers, transfer on each call, and chunked transfers.
- Verify that repeated resident calls do not use stale input/output data or leak device allocations.
- Test at least two data sizes and compare complete output arrays, not only summary metrics.
- Make the test fail if the expected MIC execution path was not entered.

### P2 — Make native runtime dependency checks strict

The deployment script's best-effort copy of a default Intel MIC library list is convenient during development, but missing libraries or an unsuccessful dependency query can remain unnoticed until a binary is launched on the card.

**Recommended actions**
- Inspect the actual dependency report for each built MIC executable.
- Require every non-optional dependency to resolve before deployment is marked successful.
- Distinguish optional-library warnings from missing required libraries.
- Confirm libraries come from the MIC target directory for the exact compiler version, not the host's ordinary `intel64` directory.
- Run a minimal native smoke test after deployment, followed by `knc_validate`.

### P2 — Guard argument parsing and size arithmetic

The offload program's size handling should reject malformed input and prevent integer overflow before calculating element counts, byte sizes, or transfer lengths. Positive-value checks alone do not protect multiplications such as `B * S` or `2 * n`.

**Recommended actions**
- Parse integers with `strtol()` (or an equivalent checked conversion), checking `errno`, end pointers, trailing characters, and range.
- Validate operands before multiplication, e.g. check `B <= LONG_MAX / S` before computing `B * S`.
- Check byte-size calculations against `SIZE_MAX` and impose a documented practical memory limit.
- Reject missing option values, unknown options, zero/negative sizes, and invalid chunk counts with a nonzero exit status.
- Add tests for overflow, malformed arguments, and allocation failure paths.

### P2 — Define and test numerical error metrics

A relative-error metric that falls back to an absolute denominator of 1 when the reference value is zero can be useful as a hybrid measure, but it should not be called conventional relative error without qualification. Values close to zero can make comparisons hard to interpret.

**Recommended actions**
- Report maximum absolute error.
- Report relative error only under a documented denominator/near-zero policy.
- Keep tolerances appropriate to the precision and operation under test.
- Test NaN and infinity behavior explicitly if those values are part of the supported input contract.

### P2 — Test chunking boundaries and invariants

Chunked transfer logic should make divisibility and size assumptions explicit rather than relying only on a benchmark's current argument restrictions.

**Recommended actions**
- Test `chunks = 1`, the maximum valid chunk count, and invalid counts (zero, negative, or greater than the number of work units).
- Verify behavior for sizes that are not evenly divisible if those sizes are intended to be supported; otherwise reject them with a clear error.
- Check offsets and transfer lengths at the first and final chunk to catch off-by-one errors.

### P2 — Separate compute time from end-to-end time

Resident-buffer, transfer-per-call, and chunked modes may measure different combinations of host-to-device transfer, kernel execution, and device-to-host transfer. These are all useful, but must not be conflated.

**Recommended actions**
- Keep separate fields for input transfer, kernel execution, output transfer, and end-to-end elapsed time where measurable.
- Document exactly which phases are included in each timing.
- Avoid comparing kernel-only timings directly with end-to-end timings.
- Use warm-up iterations and repeat measurements; retain raw observations as well as summary statistics.

### P2 — Preserve sweep failures as failures

Emitting an `unavailable` record when an SSH command fails can make partial sweeps inspectable, but an output file containing valid JSON is not evidence that all required runs completed.

**Recommended actions**
- Record command, exit status, stderr, and configuration for each failed run.
- Summarize successful, unavailable, invalid, and missing records at the end of the sweep.
- Return a nonzero overall exit status when required configurations fail.
- Keep unavailable records, but exclude them from performance summaries and never convert them to zero-valued measurements.

### P2 — Record consistent build provenance

Native and offload results should carry enough provenance to reproduce the exact build. A commit hash alone is insufficient when the working tree contains uncommitted changes.

**Recommended actions**
- Record commit SHA, dirty-tree state, compiler version, compiler flags, target mode, MPSS version, host/guest OS and kernel, and relevant OpenMP settings.
- Use the same provenance schema for native and offload results.
- Store complete build logs with the run artifacts.
- Ensure every result is explicitly identified as host, KNC-native, or MIC offload.

## First real-card preflight

Complete these steps in order; stop and fix any failure before running the full sweep.

- [ ] Record guest OS, kernel, MPSS version, Intel compiler version, and card identification.
- [ ] Compile and run a minimal KNC-native `-mmic` smoke test.
- [ ] Build all native tools from a clean output directory.
- [ ] Inspect dependencies and deploy required MIC runtime libraries.
- [ ] Run `knc_validate` against a small known-good dataset.
- [ ] Compile the offload executable with the actual Intel compiler; retain the full log and offload report.
- [ ] Run each offload pattern on a small deterministic input and compare every output element to the host reference.
- [ ] Verify requested and observed MIC-side thread counts.
- [ ] Test size, chunk, and invalid-argument boundaries.
- [ ] Run a small benchmark matrix and inspect raw JSON, logs, status counts, and provenance.
- [ ] Only then run the full sweep and collect the host baseline on the same physical host.

## Evidence required before claiming Milestone 2

Do not claim real KNC acceleration or publish MIC performance conclusions until all applicable items below have evidence attached to the run:

- A native executable built with the intended Intel compiler and executed on the physical 5110P.
- A successful native validation run with output correctness checks.
- An offload executable built with verified Intel offload support and executed on the MIC.
- Correctness results for every offload transfer pattern.
- Raw benchmark records, failure/status summary, build logs, compiler/runtime versions, and provenance.
- A host baseline collected on the same lab host under documented conditions.

## Notes and limitations

This is a source-review checklist, not a substitute for a hardware test. Some risks above are inferred from code paths and unverified compatibility assumptions; they should be confirmed or dismissed through targeted tests. Do not describe them as reproduced defects until a failing test or hardware run demonstrates one.
