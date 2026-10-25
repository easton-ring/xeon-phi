#!/bin/sh
# Copy native KNC binaries, their runtime libraries and one dataset split to the
# card, then smoke-test them there.                             *** UNVERIFIED ***
# Run in the MPSS guest after `make -C mic/native [TOOLCHAIN=...]`. Requires a
# working `ssh mic0`. It only copies files and runs the tools; it changes no
# host or card configuration.
#
#   MIC_LIBDIR=<dir>[:<dir>...] mic/native/deploy.sh data/dev/test.kncds [mic0] [/tmp/knc]
#
# MIC_LIBDIR: where the k1om builds of the runtime libraries live, e.g.
#   icc 17:    .../compiler/lib/intel64_lin_mic   (forum report; Composer XE: .../compiler/lib/mic)
#   gcc-k1om:  the cross-compiler's sysroot lib dir, e.g. /usr/linux-k1om-4.7/linux-k1om/lib64 (forum report)
#   find them with: find /opt/intel /usr/linux-k1om-* -name 'libiomp5.so' -o -name 'libgomp.so*' 2>/dev/null
#
# Library policy (strict): the binaries' own NEEDED entries (readelf -d) decide.
# glibc libraries are already on the card (Intel's native-build article) and are
# not copied; every other NEEDED library must be found in MIC_LIBDIR, otherwise
# the script stops before touching the card. EXTRA_LIBS adds libraries loaded at
# run time without a NEEDED entry.
#
# Other notes from Intel's native-build article (docs/mpss-compatibility.md):
#   - the card's filesystem is a RAM disk in GDDR5: copied data uses card memory
#     (the dev test split is 13 MB of 8 GB); use an operator-set-up NFS share for big data;
#   - micnativeloadex is not meant for performance measurement, so benchmarks run over ssh.
set -eu
DATASET=${1:?usage: MIC_LIBDIR=... deploy.sh DATASET.kncds [CARD] [DIR]}
CARD=${2:-mic0}
DIR=${3:-/tmp/knc}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${OUT:-$ROOT/build-mic}
MIC_LIBDIR=${MIC_LIBDIR:?set MIC_LIBDIR to the k1om runtime library directory (see comment above)}
TOOLS="knc_bench knc_validate knc_infer"
GLIBC_RE='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|libgcc_s\.so\.1|ld-linux.*)$'
die() { echo "deploy.sh: ERROR: $*" >&2; exit 1; }

# 1. Binaries exist and are K1OM
for t in $TOOLS; do
    [ -f "$BIN/$t" ] || die "$BIN/$t missing; run make -C mic/native first"
    [ "$(sh "$ROOT/mic/toolchain/elf-target.sh" "$BIN/$t")" = K1OM ] || die "$BIN/$t is not a K1OM executable"
done
[ -f "$DATASET" ] || die "$DATASET not found"

# 2. Resolve every non-glibc NEEDED library in MIC_LIBDIR
needed=$(for t in $TOOLS; do readelf -d "$BIN/$t"; done | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | sort -u)
libs=""
missing=""
for l in $(printf '%s\n' $needed ${EXTRA_LIBS:-} | sort -u); do
    if printf '%s\n' "$l" | grep -Eq "$GLIBC_RE"; then
        echo "  $l: glibc, expected on the card"
        continue
    fi
    found=""
    old_ifs=$IFS; IFS=:
    for d in $MIC_LIBDIR; do
        if [ -f "$d/$l" ]; then found="$d/$l"; break; fi
    done
    IFS=$old_ifs
    if [ -n "$found" ]; then
        t=$(sh "$ROOT/mic/toolchain/elf-target.sh" "$found" || true)
        [ "$t" = K1OM ] || die "$found is $t, not a k1om library (wrong MIC_LIBDIR?)"
        echo "  $l: $found"
        libs="$libs $found"
    else
        missing="$missing $l"
    fi
done
[ -z "$missing" ] || die "required libraries not found in MIC_LIBDIR=$MIC_LIBDIR:$missing"

# Informational: Intel's own dependency report, when available (exit status kept)
if [ -x /opt/intel/mic/bin/micnativeloadex ]; then
    rc=0
    SINK_LD_LIBRARY_PATH=$MIC_LIBDIR /opt/intel/mic/bin/micnativeloadex "$BIN/knc_bench" -l \
        > "$BIN/knc_bench.deps.txt" 2>&1 || rc=$?
    echo "micnativeloadex -l: exit $rc (log $BIN/knc_bench.deps.txt)"
fi

# 3. Copy and smoke-test on the card
ssh "$CARD" "mkdir -p $DIR"
# shellcheck disable=SC2086  # $libs is a space-separated list of paths without spaces
scp $libs "$BIN/knc_bench" "$BIN/knc_validate" "$BIN/knc_infer" "$DATASET" "$CARD:$DIR/"
ssh "$CARD" "cd $DIR && chmod +x $TOOLS"
smoke=$(ssh "$CARD" "cd $DIR && LD_LIBRARY_PATH=$DIR ./knc_bench --kernel magsq --synthetic --samples 64 --batch 1 --threads 1 --warmup 0 --reps 1") \
    || die "smoke run of knc_bench on $CARD failed"
printf '%s\n' "$smoke" | grep -q '"status": "ok"' || die "smoke run did not report status ok: $smoke"
echo "smoke: knc_bench ran on $CARD"
ssh "$CARD" "cd $DIR && LD_LIBRARY_PATH=$DIR ./knc_validate $(basename "$DATASET")" \
    || die "knc_validate on $CARD rejected $(basename "$DATASET")"
echo "deployed to $CARD:$DIR"
