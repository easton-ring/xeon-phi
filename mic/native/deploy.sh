#!/bin/sh
# Copy native KNC binaries and one dataset split to the card.   *** UNVERIFIED ***
# Run in the MPSS guest after `make -C mic/native`. Requires a working
# `ssh mic0` (MPSS user setup). It only copies files; it changes no host or
# card configuration.
#
#   mic/native/deploy.sh data/dev/test.kncds [mic0] [/tmp/knc]
set -eu
DATASET=${1:?usage: deploy.sh DATASET.kncds [CARD] [DIR]}
CARD=${2:-mic0}
DIR=${3:-/tmp/knc}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${OUT:-$ROOT/build-mic}
# The card needs the KNC build of the Intel OpenMP runtime (libiomp5.so).
# icc 2017: .../compiler/lib/intel64_lin_mic/libiomp5.so; older Composer XE:
# .../compiler/lib/mic/libiomp5.so (forum-sourced, docs/mpss-compatibility.md).
# Find it with: find /opt/intel -name libiomp5.so -path '*mic*'
#
# Alternative without ssh/scp of libraries: micnativeloadex copies the binary
# and its library dependencies (but not the dataset) to the card:
#   SINK_LD_LIBRARY_PATH=<dir of the MIC libiomp5.so> micnativeloadex build-mic/knc_bench -a "--kernel fft --synthetic"
MIC_LIBIOMP=${MIC_LIBIOMP:?set MIC_LIBIOMP to the k1om libiomp5.so (see comment above)}

ssh "$CARD" "mkdir -p $DIR"
scp "$BIN/knc_bench" "$BIN/knc_validate" "$BIN/knc_infer" "$MIC_LIBIOMP" "$DATASET" "$CARD:$DIR/"
ssh "$CARD" "cd $DIR && LD_LIBRARY_PATH=$DIR ./knc_validate $(basename "$DATASET")"
echo "deployed to $CARD:$DIR"
