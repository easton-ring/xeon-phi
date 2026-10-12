#!/bin/sh
# Copy native KNC binaries, their Intel runtime libraries and one dataset split
# to the card.                                                  *** UNVERIFIED ***
# Run in the MPSS guest after `make -C mic/native`. Requires a working
# `ssh mic0` (MPSS user setup). It only copies files; it changes no host or
# card configuration.
#
#   MIC_LIBDIR=<MIC runtime lib dir> mic/native/deploy.sh data/dev/test.kncds [mic0] [/tmp/knc]
#
# Source for the procedure: Intel, "Building a Native Application for Intel
# Xeon Phi Coprocessors" (2014; docs/mpss-compatibility.md, "Native build
# article"). Points taken from it:
#   - icc -mmic binaries need the compiler's MIC runtime libraries on the card;
#     typical set: libiomp5.so libimf.so libsvml.so libintlc.so.5 libirng.so
#     (glibc libraries are already on the card).
#   - Composer XE 2013 SP1 installs them in .../compiler/lib/mic/. For icc 2017
#     a forum report gives .../compiler/lib/intel64_lin_mic/. Find yours with
#       find /opt/intel -name libimf.so -path '*mic*'
#   - `micnativeloadex BINARY -l` (with SINK_LD_LIBRARY_PATH=MIC_LIBDIR) lists
#     the real dependencies; this script logs that list when available.
#   - The card's local filesystem is a RAM disk in GDDR5: the dataset copied
#     to /tmp takes card memory away from the benchmark (the dev test split is
#     13 MB of the 8 GB). For large datasets use an NFS share instead
#     (operator setup; it edits host /etc/exports and is not scripted here).
#   - micnativeloadex is "not intended for use when measuring application
#     performance", so benchmarks run via ssh (run_sweep.sh), not micnativeloadex.
set -eu
DATASET=${1:?usage: MIC_LIBDIR=... deploy.sh DATASET.kncds [CARD] [DIR]}
CARD=${2:-mic0}
DIR=${3:-/tmp/knc}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${OUT:-$ROOT/build-mic}
MIC_LIBDIR=${MIC_LIBDIR:?set MIC_LIBDIR to the compiler MIC runtime library directory (see comment above)}
LIBS=${LIBS:-"libiomp5.so libimf.so libsvml.so libintlc.so.5 libirng.so"}

[ -f "$MIC_LIBDIR/libiomp5.so" ] || { echo "deploy.sh: $MIC_LIBDIR/libiomp5.so not found" >&2; exit 1; }

# Log the real dependency list (informational; the script still copies $LIBS).
if [ -x /opt/intel/mic/bin/micnativeloadex ]; then
    SINK_LD_LIBRARY_PATH=$MIC_LIBDIR /opt/intel/mic/bin/micnativeloadex "$BIN/knc_bench" -l \
        | tee "$BIN/knc_bench.deps.txt" || true
fi

set --
for l in $LIBS; do
    if [ -f "$MIC_LIBDIR/$l" ]; then
        set -- "$@" "$MIC_LIBDIR/$l"
    else
        echo "deploy.sh: warning: $MIC_LIBDIR/$l not found (may not be needed; check the -l list)" >&2
    fi
done

ssh "$CARD" "mkdir -p $DIR"
scp "$BIN/knc_bench" "$BIN/knc_validate" "$BIN/knc_infer" "$@" "$DATASET" "$CARD:$DIR/"
ssh "$CARD" "cd $DIR && chmod +x knc_bench knc_validate knc_infer && LD_LIBRARY_PATH=$DIR ./knc_validate $(basename "$DATASET")"
echo "deployed to $CARD:$DIR"
