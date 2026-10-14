#!/bin/sh
# Functional ladder for the KNC host-driver port (docs/kernel-port.md section 6).
#                                                               *** UNVERIFIED ***
# Never run against a real card. Runs in the DEVELOPMENT GUEST (card passed
# through), as root, after the ported modules are installed for the running
# kernel. It loads/unloads kernel modules and boots/stops the card, so it
# refuses to run on bare metal unless ALLOW_BARE_METAL=1.
#
#   KNC_PORT_DEV_GUEST=1 mic/port/ladder.sh
#
# Stops at the first failing rung. Writes
#   results/port/<uname -r>/<UTC timestamp>/{ladder.json,L*.log,dmesg-L*.txt}
# A rung also fails if new dmesg lines match BUG/Oops/WARNING/KASAN/DMA-API/
# "general protection"/"soft lockup"/"hung task".
#
# Environment (defaults in brackets):
#   MODULES      modules to load, in order [mic]   (MPSS 3.x host module name)
#   CARD         card host name [mic0]
#   BOOT_TIMEOUT seconds to wait for "online" [300]
#   CYCLES       load/boot/stop/unload cycles in L8 [3]
#   KNC_DIR      card directory with knc_bench + libs from mic/native/deploy.sh [/tmp/knc]
#   OFFLOAD_BIN  offload benchmark binary [build-mic/offload_magsq]
#   SCIF_TEST    command for L5 (e.g. a SCIF ping-pong test); unset = skip L5
#   MAX_RUNG     stop after this rung number [8]
set -u

[ "${KNC_PORT_DEV_GUEST:-0}" = 1 ] || { echo "set KNC_PORT_DEV_GUEST=1 to confirm this is the dev guest" >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo "run as root" >&2; exit 2; }
if ! grep -q ' hypervisor' /proc/cpuinfo && [ "${ALLOW_BARE_METAL:-0}" != 1 ]; then
    echo "no hypervisor flag: this looks like bare metal. Develop in a VM first (docs/kernel-port.md section 3)," >&2
    echo "or set ALLOW_BARE_METAL=1 if you really mean it." >&2
    exit 2
fi

MODULES=${MODULES:-mic}
CARD=${CARD:-mic0}
BOOT_TIMEOUT=${BOOT_TIMEOUT:-300}
CYCLES=${CYCLES:-3}
KNC_DIR=${KNC_DIR:-/tmp/knc}
OFFLOAD_BIN=${OFFLOAD_BIN:-build-mic/offload_magsq}
MAX_RUNG=${MAX_RUNG:-8}
OUT=results/port/$(uname -r)/$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "$OUT"
RESULTS=""
BAD_DMESG='BUG:|Oops|WARNING:|KASAN|DMA-API|general protection|soft lockup|hung task|Call Trace'

record() {  # record RUNG STATUS DETAIL
    RESULTS="$RESULTS{\"rung\": \"$1\", \"status\": \"$2\", \"detail\": \"$(printf '%s' "$3" | tr '"\\' "''")\"},"
    printf '%-4s %-5s %s\n' "$1" "$2" "$3"
}

finish() {
    {
        printf '{"kernel": "%s", "time_utc": "%s", "modules": "%s", "hypervisor_flag": %s, "rungs": [' \
            "$(uname -r)" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$MODULES" \
            "$(grep -q ' hypervisor' /proc/cpuinfo && echo true || echo false)"
        printf '%s' "${RESULTS%,}"
        printf ']}\n'
    } > "$OUT/ladder.json"
    echo "results: $OUT/ladder.json"
    exit "$1"
}

# run_rung ID DESCRIPTION SHELL-COMMAND : pass = exit 0 and no new bad dmesg lines
run_rung() {
    id=$1; desc=$2; cmd=$3
    before=$(dmesg | wc -l)
    sh -c "$cmd" > "$OUT/$id.log" 2>&1
    rc=$?
    dmesg | tail -n +"$((before + 1))" > "$OUT/dmesg-$id.txt"
    if grep -Eq "$BAD_DMESG" "$OUT/dmesg-$id.txt"; then
        record "$id" FAIL "$desc: kernel reported a problem (see dmesg-$id.txt)"
        finish 1
    fi
    if [ "$rc" -ne 0 ]; then
        record "$id" FAIL "$desc: exit $rc (see $id.log)"
        finish 1
    fi
    record "$id" PASS "$desc"
}

stop_card() { systemctl stop mpss 2>/dev/null || service mpss stop; }
unload_modules() {
    rev=""
    for m in $MODULES; do rev="$m $rev"; done
    for m in $rev; do modprobe -r "$m" || return 1; done
}

echo "ladder: kernel $(uname -r), modules '$MODULES', output $OUT"

# L0 environment
run_rung L0 "card visible, BAR0 assigned" \
    "lspci -nn -d 8086:2250 | grep -q 8086:2250 && lspci -vvv -d 8086:2250 | grep 'Region 0: Memory at'"
[ "$MAX_RUNG" -ge 1 ] || finish 0

# L1 module load (start from a clean state)
stop_card >/dev/null 2>&1 || true
unload_modules >/dev/null 2>&1 || true
run_rung L1 "modules load ($MODULES)" "for m in $MODULES; do modprobe \$m || exit 1; done; lsmod"
[ "$MAX_RUNG" -ge 2 ] || finish 0

# L2 card ready
run_rung L2 "micctrl -s reports ready" "micctrl -s; micctrl -s | grep -q -e ready -e online"
[ "$MAX_RUNG" -ge 3 ] || finish 0

# L3 card OS boot
run_rung L3 "card OS boots to online (<= ${BOOT_TIMEOUT}s)" \
    "systemctl start mpss 2>/dev/null || service mpss start; i=0; while [ \$i -lt $BOOT_TIMEOUT ]; do micctrl -s | grep -q online && exit 0; sleep 5; i=\$((i+5)); done; micctrl -s; exit 1"
[ "$MAX_RUNG" -ge 4 ] || finish 0

# L4 network
run_rung L4 "ping + ssh $CARD" \
    "ping -c3 -W2 $CARD && ssh -o BatchMode=yes -o ConnectTimeout=10 $CARD 'uname -a'"
[ "$MAX_RUNG" -ge 5 ] || finish 0

# L5 SCIF
if [ -n "${SCIF_TEST:-}" ]; then
    run_rung L5 "SCIF test: $SCIF_TEST" "$SCIF_TEST"
else
    record L5 SKIP "no SCIF_TEST command given"
fi
[ "$MAX_RUNG" -ge 6 ] || finish 0

# L6 native compute on the card: FFT f32, max_rel <= 5*log2(1024)*FLT_EPSILON (docs/kernels.md)
run_rung L6 "knc_bench fft on $CARD within error bound" \
    "r=\$(ssh -o BatchMode=yes $CARD 'cd $KNC_DIR && LD_LIBRARY_PATH=$KNC_DIR ./knc_bench --kernel fft --synthetic --samples 1024 --batch 8 --threads 60 --reps 3') || exit 1
     echo \"\$r\"
     echo \"\$r\" | grep -q '\"status\": \"ok\"' || exit 1
     echo \"\$r\" | grep -q '\"execution_mode\": \"mic-native\"' || exit 1
     m=\$(echo \"\$r\" | sed -n 's/.*\"max_rel\": \\([0-9.eE+-]*\\).*/\\1/p')
     awk -v m=\"\$m\" 'BEGIN { exit !(m != \"\" && m <= 5*10*1.1920929e-07) }'"
[ "$MAX_RUNG" -ge 7 ] || finish 0

# L7 offload
if [ -x "$OFFLOAD_BIN" ]; then
    run_rung L7 "offload_magsq runs on the card" \
        "out=\$($OFFLOAD_BIN --n-examples 64 --samples 1024 --reps 3 --chunks 4) || exit 1; echo \"\$out\"; [ \"\$(echo \"\$out\" | grep -c '\"status\": \"ok\"')\" -eq 3 ]"
else
    record L7 SKIP "$OFFLOAD_BIN not built (make -C mic/offload)"
fi
[ "$MAX_RUNG" -ge 8 ] || finish 0

# L8 teardown and soak: stop/unload, then CYCLES x (load, boot, stop, unload)
run_rung L8a "stop card and unload modules" \
    "systemctl stop mpss 2>/dev/null || service mpss stop; rev=''; for m in $MODULES; do rev=\"\$m \$rev\"; done; for m in \$rev; do modprobe -r \$m || exit 1; done"
c=1
while [ "$c" -le "$CYCLES" ]; do
    run_rung "L8c$c" "cycle $c/$CYCLES: load, boot, stop, unload" \
        "for m in $MODULES; do modprobe \$m || exit 1; done
         systemctl start mpss 2>/dev/null || service mpss start
         i=0; ok=0; while [ \$i -lt $BOOT_TIMEOUT ]; do micctrl -s | grep -q online && { ok=1; break; }; sleep 5; i=\$((i+5)); done
         [ \$ok = 1 ] || exit 1
         systemctl stop mpss 2>/dev/null || service mpss stop
         rev=''; for m in $MODULES; do rev=\"\$m \$rev\"; done; for m in \$rev; do modprobe -r \$m || exit 1; done"
    c=$((c + 1))
done
finish 0
