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
# Its location depends on the compiler install; set MIC_LIBIOMP explicitly.
MIC_LIBIOMP=${MIC_LIBIOMP:?set MIC_LIBIOMP to the k1om libiomp5.so (e.g. under .../compiler/lib/mic/) - path UNVERIFIED}

ssh "$CARD" "mkdir -p $DIR"
scp "$BIN/knc_bench" "$BIN/knc_validate" "$BIN/knc_infer" "$MIC_LIBIOMP" "$DATASET" "$CARD:$DIR/"
ssh "$CARD" "cd $DIR && LD_LIBRARY_PATH=$DIR ./knc_validate $(basename "$DATASET")"
echo "deployed to $CARD:$DIR"
