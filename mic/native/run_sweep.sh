#!/bin/sh
# Native thread/batch sweep on the card; writes JSON lines on the guest.   *** UNVERIFIED ***
#
#   mic/native/run_sweep.sh test.kncds [mic0] [/tmp/knc] > results/mic-native-raw.jsonl
#   python -m knc_rf_lab.benchmark --collect results/mic-native-raw.jsonl --out results/bench-mic-native.json
#
# Affinity (Intel OpenMP on KNC; semantics UNVERIFIED for this card/runtime,
# see docs/mpss-compatibility.md):
#   KMP_AFFINITY=balanced   spread threads over cores, then fill hardware threads
#   KMP_PLACE_THREADS=<c>c,<t>t   restrict to c cores x t threads per core
# Thread points: 1 2 4 8 16 30 60 120 240 (240 = 60 cores x 4 threads; one core
# is often left to the card OS - compare 236 as well).
# Runs over ssh on purpose: Intel's native-build article says micnativeloadex is
# "not intended for use when measuring application performance".
set -eu
DATASET=${1:?usage: run_sweep.sh DATASET_BASENAME [CARD] [DIR]}
CARD=${2:-mic0}
DIR=${3:-/tmp/knc}
KERNELS=${KERNELS:-"cmul magsq meanpow psd fft xcorr cfo features"}
BATCHES=${BATCHES:-"1 16 64 256 1024"}
THREADS=${THREADS:-"1 2 4 8 16 30 60 120 236 240"}
AFFINITY=${AFFINITY:-balanced}
REPS=${REPS:-20}

for k in $KERNELS; do
  for p in f32 f64; do
    [ "$k" = features ] && [ "$p" = f64 ] && continue
    for b in $BATCHES; do
      for t in $THREADS; do
        ssh "$CARD" "cd $DIR && LD_LIBRARY_PATH=$DIR KMP_AFFINITY=$AFFINITY OMP_NUM_THREADS=$t \
          ./knc_bench --kernel $k --dataset $DATASET --batch $b --threads $t --precision $p --reps $REPS" \
          || echo "{\"schema_version\": \"1.0\", \"status\": \"unavailable\", \"execution_mode\": \"mic-native\", \"kernel\": \"$k\", \"threads\": $t, \"reason\": \"knc_bench failed on $CARD (exit $?)\"}"
      done
    done
  done
done
