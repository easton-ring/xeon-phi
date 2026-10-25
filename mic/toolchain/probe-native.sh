#!/bin/sh
# Best-effort object-code probes. Does not install tools or run binaries on KNC.
# Usage: sh mic/toolchain/probe-native.sh [OUTDIR]
# Optional: KNC_CC=/path/to/compiler KNC_CFLAGS='-mmic ...'
set -u

STAMP=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo unknown-time)
OUT=${1:-results/toolchain/$STAMP-probe}
HERE=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
SRC="$HERE/knc-smoke.c"
OMP_SRC="$HERE/knc-omp-smoke.c"
VEC_SRC="$HERE/knc-vec-smoke.c"
mkdir -p "$OUT" || exit 1
SUMMARY="$OUT/SUMMARY.txt"
: > "$SUMMARY"

say() {
    printf '%s\n' "$*" | tee -a "$SUMMARY"
}

show_artifact() {
    name=$1
    obj=$2
    say "Artifact: $obj"
    if command -v file >/dev/null 2>&1; then
        { file "$obj"; } 2>&1 | tee "$OUT/$name-file.txt" | tee -a "$SUMMARY"
    else
        say "file utility unavailable"
    fi
    if command -v readelf >/dev/null 2>&1; then
        { readelf -h "$obj"; } 2>&1 | tee "$OUT/$name-readelf.txt" | tee -a "$SUMMARY"
    else
        say "readelf unavailable"
    fi
    if command -v objdump >/dev/null 2>&1; then
        { objdump -f "$obj"; } 2>&1 | tee "$OUT/$name-objdump.txt" | tee -a "$SUMMARY"
    else
        say "objdump unavailable"
    fi
    # Decide from the ELF header (mic/toolchain/elf-target.sh), never by grepping tool
    # output: file and objdump print the path, and probe names may contain "k1om".
    t=$(sh "$HERE/elf-target.sh" "$obj")
    say "ELF e_machine: $t"
    if [ "$t" = K1OM ]; then
        say "TARGET_VERDICT: K1OM_CONFIRMED for $name"
        return 0
    fi
    say "TARGET_VERDICT: NOT_CONFIRMED for $name (host object or metadata inconclusive)"
    return 1
}

# probe NAME CC [FLAGS...] : compile $SRC to an object (-c) and check its target
probe() {
    probe_build obj "$SRC" "$@"
}

# probe_build MODE SOURCE NAME CC [FLAGS...]
#   MODE obj: -c -> NAME.o;  exe: full link -> NAME (never executed)
probe_build() {
    mode=$1
    src=$2
    name=$3
    shift 3
    if [ "$mode" = obj ]; then out="$OUT/$name.o"; set -- "$@" -c; else out="$OUT/$name"; fi
    log="$OUT/$name-build.log"
    say ""
    say "=== $name ($mode) ==="
    printf 'Command:' > "$log"
    for arg do printf ' %s' "$arg" >> "$log"; done
    printf ' %s -o %s\n' "$src" "$out" >> "$log"
    "$@" "$src" -o "$out" >> "$log" 2>&1
    rc=$?
    tee -a "$SUMMARY" < "$log"
    if [ "$rc" -ne 0 ]; then
        say "BUILD_VERDICT: FAIL (exit $rc)"
        return 1
    fi
    say "BUILD_VERDICT: PASS ($mode produced)"
    show_artifact "$name" "$out"
}

# vec_probe NAME CC [FLAGS...] : compile the vector loop to assembly, look for %zmm
vec_probe() {
    name=$1
    shift
    asm="$OUT/$name.s"
    say ""
    say "=== $name (vectorization, assembly only) ==="
    if ! "$@" -O3 -S "$VEC_SRC" -o "$asm" > "$OUT/$name-build.log" 2>&1; then
        tee -a "$SUMMARY" < "$OUT/$name-build.log"
        say "VECTOR_VERDICT: UNKNOWN for $name (assembly build failed)"
        return 1
    fi
    if grep -q '%zmm' "$asm"; then
        say "VECTOR_VERDICT: ZMM_USED for $name (512-bit vector registers in generated code)"
    else
        say "VECTOR_VERDICT: NO_ZMM for $name (no 512-bit vector registers; expect scalar or narrower code)"
    fi
}

# cross_probe CC : full set of checks for one k1om cross-compiler candidate
cross_probe() {
    cc=$1
    tag=$2
    say ""
    say "##### k1om cross-compiler candidate: $cc"
    { "$cc" --version; echo "dumpmachine: $("$cc" -dumpmachine 2>&1)"; echo "print-sysroot: $("$cc" -print-sysroot 2>&1)"; } \
        > "$OUT/$tag-version.txt" 2>&1
    tee -a "$SUMMARY" < "$OUT/$tag-version.txt"
    "$cc" -dM -E - < /dev/null > "$OUT/$tag-macros.txt" 2>&1
    say "Predefined macros mentioning MIC/K1OM/KNC/AVX/SSE (full list: $tag-macros.txt):"
    grep -Ei 'mic|k1om|knc|avx|sse|512' "$OUT/$tag-macros.txt" | tee -a "$SUMMARY" || say "  (none)"
    if grep -q '__MIC__' "$OUT/$tag-macros.txt"; then
        say "MACRO_VERDICT: __MIC__ defined"
    else
        say "MACRO_VERDICT: __MIC__ NOT defined (mic/native gcc-k1om profile relies on an ELF check instead)"
    fi
    probe_build obj "$SRC" "$tag-obj" "$cc"
    probe_build exe "$SRC" "$tag-exe" "$cc"
    if probe_build exe "$OMP_SRC" "$tag-omp-exe" "$cc" -fopenmp; then
        if command -v readelf >/dev/null 2>&1; then
            say "Dynamic dependencies of $tag-omp-exe:"
            readelf -d "$OUT/$tag-omp-exe" 2>/dev/null | grep NEEDED | tee -a "$SUMMARY"
        fi
    else
        say "OPENMP_VERDICT: FAIL for $tag (no usable OpenMP runtime for this target?)"
    fi
    vec_probe "$tag-vec" "$cc"
}

say "KNC native code-generation probes"
say "UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)"
say "Source: $SRC"
say "Output: $OUT"
say "A successful compile is not proof of KNC code generation; K1OM metadata is required."
say "No generated binary is executed."

if command -v gcc >/dev/null 2>&1; then
    probe host-gcc gcc
else
    say "SKIP: gcc not found"
fi

if command -v icc >/dev/null 2>&1; then
    probe intel-icc-mm icc -mmic
else
    say "SKIP: icc not found (Intel KNC compiler probe unavailable)"
fi

if command -v gcc >/dev/null 2>&1; then
    probe gcc-mmic gcc -mmic
else
    say "SKIP: gcc not found for -mmic probe"
fi

if command -v clang >/dev/null 2>&1; then
    probe clang-k1om clang --target=k1om-linux-gnu
else
    say "SKIP: clang not found"
fi

if [ -n "${KNC_CC:-}" ]; then
    # Intentionally split KNC_CFLAGS into compiler arguments. Use only trusted
    # local compiler paths and ordinary flags supplied by the operator.
    # shellcheck disable=SC2086
    # shellcheck disable=SC2090
    if [ -n "${KNC_CFLAGS:-}" ]; then
        # shellcheck disable=SC2086
        probe custom-knc "$KNC_CC" $KNC_CFLAGS
    else
        probe custom-knc "$KNC_CC"
    fi
else
    say "SKIP: custom probe (set KNC_CC and optionally KNC_CFLAGS to try one)"
fi

# k1om GCC cross-compilers shipped with MPSS (forum-sourced locations, outside PATH)
# or given explicitly with KNC_CROSS_CC.
if [ -n "${KNC_CROSS_CC:-}" ]; then
    CROSS_LIST=$KNC_CROSS_CC
else
    CROSS_LIST=$(
        for d in /usr/linux-k1om-* /opt/mpss; do
            [ -d "$d" ] && find "$d" -maxdepth 8 \( -type f -o -type l \) -name '*k1om*-gcc' 2>/dev/null
        done
        for t in x86_64-k1om-linux-gcc k1om-mpss-linux-gcc; do command -v "$t" 2>/dev/null; done
    )
fi
n=0
for cc in $CROSS_LIST; do
    [ -x "$cc" ] || continue
    n=$((n + 1))
    [ "$n" -le 5 ] || { say "(more than 5 candidates; stopping)"; break; }
    cross_probe "$cc" "k1om-cross-$n"
done
[ "$n" -gt 0 ] || say "SKIP: no k1om cross-compiler found (looked in /usr/linux-k1om-*, /opt/mpss, PATH; set KNC_CROSS_CC to try one)"

if command -v icc >/dev/null 2>&1; then
    vec_probe intel-icc-mmic-vec icc -mmic
fi
vec_probe host-gcc-vec gcc -march=native

say ""
say "Interpretation:"
say "- K1OM_CONFIRMED means object metadata positively identifies Intel K1OM."
say "- NOT_CONFIRMED is not a pass, even if compilation succeeded."
say "- A K1OM object is only code-generation evidence; a K1OM executable is linked but NOT card-tested."
say "- VECTOR_VERDICT NO_ZMM for a k1om compiler means its code will not use the KNC 512-bit vector unit;"
say "  benchmark results built with it are scalar and must be labelled so (docs/benchmark-methodology.md)."
say "Results saved to: $OUT"
exit 0
