#!/bin/sh
# Best-effort object-code probes. Does not install tools or run binaries on KNC.
# Usage: sh mic/toolchain/probe-native.sh [OUTDIR]
# Optional: KNC_CC=/path/to/compiler KNC_CFLAGS='-mmic ...'
set -u

STAMP=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo unknown-time)
OUT=${1:-results/toolchain/$STAMP-probe}
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
SRC="$HERE/knc-smoke.c"
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
    if { command -v readelf >/dev/null 2>&1 && readelf -h "$obj" 2>/dev/null | grep -Eiq 'K1OM|Intel K1OM'; } ||
       { command -v file >/dev/null 2>&1 && file "$obj" 2>/dev/null | grep -Eiq 'K1OM|k1om'; } ||
       { command -v objdump >/dev/null 2>&1 && objdump -f "$obj" 2>/dev/null | grep -Eiq 'K1OM|k1om'; }; then
        say "TARGET_VERDICT: K1OM_CONFIRMED for $name"
        return 0
    fi
    say "TARGET_VERDICT: NOT_CONFIRMED for $name (host object or metadata inconclusive)"
    return 1
}

probe() {
    name=$1
    shift
    obj="$OUT/$name.o"
    log="$OUT/$name-build.log"
    say ""
    say "=== $name ==="
    printf 'Command:' > "$log"
    for arg do printf ' %s' "$arg" >> "$log"; done
    printf '\n' >> "$log"
    "$@" -c "$SRC" -o "$obj" >> "$log" 2>&1
    rc=$?
    cat "$log" | tee -a "$SUMMARY"
    if [ "$rc" -ne 0 ]; then
        say "BUILD_VERDICT: FAIL (exit $rc)"
        return 1
    fi
    say "BUILD_VERDICT: PASS (object produced)"
    show_artifact "$name" "$obj"
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

say ""
say "Interpretation:"
say "- K1OM_CONFIRMED means object metadata positively identifies Intel K1OM."
say "- NOT_CONFIRMED is not a pass, even if compilation succeeded."
say "- A K1OM object is only code-generation evidence; it is not a linked or card-tested executable."
say "Results saved to: $OUT"
exit 0
