#!/bin/sh
# Capture MPSS / card state for docs/status.md evidence.        *** UNVERIFIED ***
# Read-only: runs status and query commands only. Run as root in the MPSS guest.
#
#   mic/guest/collect-diagnostics.sh [OUTDIR]     (default results/hw/<UTC date>/diag)
set -u
OUT=${1:-results/hw/$(date -u +%Y-%m-%d)/diag}
mkdir -p "$OUT"
run() {  # run NAME CMD... : save stdout+stderr and the exit status
    name=$1; shift
    { echo "\$ $*"; "$@" 2>&1; echo "[exit $?]"; } > "$OUT/$name.txt"
    echo "  $OUT/$name.txt"
}
echo "collecting into $OUT"
run uname           uname -a
run os-release      cat /etc/centos-release /etc/redhat-release
run kernel-cmdline  cat /proc/cmdline
# shellcheck disable=SC2016  # expanded by the inner sh, on purpose
run lspci-card      sh -c 'lspci -nn | grep -i "8086:2250"; for d in $(lspci -D -d 8086:2250 | cut -d" " -f1); do lspci -vvv -s "$d"; done'
run lsmod-mic       sh -c 'lsmod | grep -i -e mic -e scif || true'
run rpm-mpss        sh -c 'rpm -qa | grep -i -e mpss -e "^mic" | sort'
run micctrl-status  micctrl -s
run micinfo         micinfo
run miccheck        miccheck
run mpss-service    sh -c 'systemctl status mpss 2>/dev/null || service mpss status'
run card-uname      ssh -o BatchMode=yes -o ConnectTimeout=10 mic0 uname -a
run card-uname-m    ssh -o BatchMode=yes -o ConnectTimeout=10 mic0 uname -m
run card-cpuinfo    ssh -o BatchMode=yes -o ConnectTimeout=10 mic0 'head -30 /proc/cpuinfo; grep -c ^processor /proc/cpuinfo'
run card-meminfo    ssh -o BatchMode=yes -o ConnectTimeout=10 mic0 'head -5 /proc/meminfo'
run icc-version     sh -c 'command -v icc && icc -V'
run mic-runtime-libs sh -c 'find /opt/intel \( -name libiomp5.so -o -name libimf.so -o -name libsvml.so -o -name libintlc.so.5 -o -name libirng.so \) -path "*mic*" 2>/dev/null'
echo "done; review the files before committing them"
