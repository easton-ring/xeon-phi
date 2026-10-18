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
#
# stdout carries JSON lines only (one record per configuration). A failed run
# becomes an "unavailable" record with its command, exit status and stderr tail;
# full stderr is kept in LOGDIR. A summary goes to stderr, and the script exits
# 1 if any configuration failed, so a complete-looking JSONL file is never
# mistaken for a complete sweep. Failed runs are never turned into numbers.
set -eu
DATASET=${1:?usage: run_sweep.sh DATASET_BASENAME [CARD] [DIR]}
CARD=${2:-mic0}
DIR=${3:-/tmp/knc}
KERNELS=${KERNELS:-"cmul magsq meanpow psd fft xcorr cfo features"}
BATCHES=${BATCHES:-"1 16 64 256 1024"}
THREADS=${THREADS:-"1 2 4 8 16 30 60 120 236 240"}
AFFINITY=${AFFINITY:-balanced}
REPS=${REPS:-20}

LOGDIR=${LOGDIR:-results/mic-native-logs-$(date -u +%Y%m%dT%H%M%SZ)}
mkdir -p "$LOGDIR"
total=0
ok=0
failed=0

json_str() {  # escape for a JSON string: backslash, quote, control chars -> space
    printf '%s' "$1" | tr '\n\r\t' '   ' | sed 's/\\/\\\\/g; s/"/\\"/g'
}

for k in $KERNELS; do
  for p in f32 f64; do
    [ "$k" = features ] && [ "$p" = f64 ] && continue
    for b in $BATCHES; do
      for t in $THREADS; do
        total=$((total + 1))
        cmd="cd $DIR && LD_LIBRARY_PATH=$DIR KMP_AFFINITY=$AFFINITY OMP_NUM_THREADS=$t ./knc_bench --kernel $k --dataset $DATASET --batch $b --threads $t --precision $p --reps $REPS"
        err="$LOGDIR/$k-$p-b$b-t$t.stderr"
        rc=0
        out=$(ssh "$CARD" "$cmd" 2> "$err") || rc=$?
        if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -q '"status": "ok"'; then
          printf '%s\n' "$out"
          ok=$((ok + 1))
        else
          failed=$((failed + 1))
          printf '{"schema_version": "1.0", "status": "unavailable", "execution_mode": "mic-native", "kernel": "%s", "threads": %s, "reason": "knc_bench failed on %s (exit %s): %s; stderr: %s"}\n' \
            "$k" "$t" "$CARD" "$rc" "$(json_str "$cmd")" "$(json_str "$(tail -c 400 "$err")")"
        fi
      done
    done
  done
done

echo "run_sweep: $total configurations, $ok ok, $failed failed (stderr logs: $LOGDIR)" >&2
[ "$failed" -eq 0 ]
