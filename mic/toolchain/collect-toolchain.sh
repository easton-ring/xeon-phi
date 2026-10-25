#!/bin/sh
# Read-only inventory for a CentOS 7 / MPSS KNC build guest.
# Usage: sh mic/toolchain/collect-toolchain.sh [OUTDIR]
set -u

STAMP=$(date -u +%Y%m%dT%H%M%SZ 2>/dev/null || echo unknown-time)
OUT=${1:-results/toolchain/$STAMP-inventory}
mkdir -p "$OUT" || exit 1

run() {
    name=$1
    shift
    file="$OUT/$name.txt"
    {
        printf '$'
        for arg do printf ' %s' "$arg"; done
        printf '\n'
        "$@" 2>&1
        rc=$?
        printf '\n[exit %s]\n' "$rc"
    } > "$file"
    printf '%-24s %s\n' "$name" "$file"
    return 0
}

run_shell() {
    name=$1
    command_text=$2
    file="$OUT/$name.txt"
    {
        printf '$ sh -c %s\n' "$command_text"
        sh -c "$command_text" 2>&1
        rc=$?
        printf '\n[exit %s]\n' "$rc"
    } > "$file"
    printf '%-24s %s\n' "$name" "$file"
    return 0
}

{
    echo "KNC toolchain inventory"
    echo "UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo unknown)"
    echo "Host: $(hostname 2>/dev/null || echo unknown)"
    echo "Output directory: $OUT"
    echo "User: $(id 2>/dev/null || echo unknown)"
    echo
    echo "This is an inventory only; no system configuration is changed."
} > "$OUT/SUMMARY.txt"

run os-release sh -c 'cat /etc/os-release /etc/centos-release /etc/redhat-release 2>/dev/null'
run kernel uname -a
run host-architecture uname -m
run cpuinfo sh -c 'head -40 /proc/cpuinfo'
run memory sh -c 'head -20 /proc/meminfo'
# One name per `command -v`: dash (Debian/Ubuntu /bin/sh) reports only the first
# of several names, bash reports all of them.
run_shell path 'for t in gcc g++ clang clang++ icc icpc as ld readelf objdump file llvm-config llvm-mc llvm-as micinfo micctrl miccheck micnativeloadex; do p=$(command -v "$t" 2>/dev/null) && printf "%-16s %s\n" "$t" "$p" || printf "%-16s <not found>\n" "$t"; done'
run gcc-version gcc --version
run gcc-target gcc -dumpmachine
run gxx-version g++ --version
run clang-version clang --version
run clang-targets sh -c 'clang --print-targets 2>&1'
run llvm-config-version llvm-config --version
run llvm-mc-version llvm-mc --version
run assembler-version as --version
run linker-version ld --version
run readelf-version readelf --version
run objdump-version objdump --version
run file-version file --version
run intel-compiler-version sh -c 'command -v icc && icc -V'
run intel-cxx-version sh -c 'command -v icpc && icpc -V'
run relevant-environment sh -c 'printf "PATH=%s\n" "$PATH"; for v in INTEL_LICENSE_FILE ONEAPI_ROOT MKLROOT MIC_LD_LIBRARY_PATH SINK_LD_LIBRARY_PATH MIC_ENV_PREFIX MIC_OMP_NUM_THREADS OMP_NUM_THREADS; do eval "x=\${$v-}"; if [ -n "$x" ]; then printf "%s=%s\n" "$v" "$x"; else printf "%s=<unset>\n" "$v"; fi; done'
run mpss-packages sh -c 'rpm -qa 2>/dev/null | grep -i -e mpss -e "^mic" | sort'
run card-status micctrl -s
run micinfo micinfo
run miccheck miccheck
run pci-card sh -c 'lspci -nn -d 8086:2250'
run ssh-card sh -c 'ssh -o BatchMode=yes -o ConnectTimeout=5 mic0 "uname -a; uname -m; grep -m1 -E \"model name|family|processor\" /proc/cpuinfo"'
run intel-mic-libraries sh -c 'find /opt/intel -type f \( -name "libiomp5.so" -o -name "libimf.so" -o -name "libsvml.so" -o -name "libintlc.so*" -o -name "libirng.so" \) -path "*mic*" -print 2>/dev/null'
# MPSS ships a GCC cross-compiler for k1om outside PATH (forum-sourced locations:
# /usr/linux-k1om-*/bin/x86_64-k1om-linux-gcc; Yocto-style SDKs under /opt/mpss).
run_shell k1om-cross-compilers 'for d in /usr/linux-k1om-* /opt/mpss /usr/local; do [ -d "$d" ] && find "$d" -maxdepth 8 \( -type f -o -type l \) -name "*k1om*-gcc" -print 2>/dev/null; done; for t in x86_64-k1om-linux-gcc k1om-mpss-linux-gcc; do command -v "$t" 2>/dev/null; done; true'
run_shell k1om-runtime-libraries 'for d in /usr/linux-k1om-* /opt/mpss; do [ -d "$d" ] && find "$d" -maxdepth 8 \( -name "libgomp.so*" -o -name "libc.so.6" -o -name "ld-linux*-k1om*" \) -print 2>/dev/null; done; true'
run standard-library-candidates sh -c 'for d in /usr/lib /usr/lib64 /lib /lib64 /usr/local/lib; do [ ! -d "$d" ] || find "$d" -maxdepth 2 -type f \( -name "*k1om*" -o -name "*mic*" \) -print 2>/dev/null; done'
run git-state sh -c 'git rev-parse HEAD 2>/dev/null; git status --short 2>/dev/null'

{
    echo
    echo "Inventory complete."
    echo "Review each *.txt file; nonzero exit statuses can mean a tool is absent or the card is offline."
    echo "No compiler probe or card modification was performed by this script."
} >> "$OUT/SUMMARY.txt"

echo "Inventory saved to: $OUT"
exit 0
