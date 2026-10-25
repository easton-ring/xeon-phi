#!/bin/sh
# Self-test for the toolchain probes; runs anywhere with gcc (used by CI).
# Checks the verdict logic, not any KNC hardware or compiler.
#   sh mic/toolchain/selftest.sh [WORKDIR]
set -u
HERE=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
W=${1:-$(mktemp -d)}
mkdir -p "$W"
fails=0
check() {  # check DESCRIPTION COMMAND...
    d=$1; shift
    if "$@"; then echo "ok   $d"; else echo "FAIL $d"; fails=$((fails + 1)); fi
}

# 1. elf-target.sh on a host object, a forged K1OM object, and a non-ELF file
gcc -c "$HERE/knc-smoke.c" -o "$W/host.o"
cp "$W/host.o" "$W/forged-k1om.o"
printf '\265\000' | dd of="$W/forged-k1om.o" bs=1 seek=18 conv=notrunc 2>/dev/null   # e_machine = 181
check "elf-target: host object is X86_64"   sh -c "[ \"\$(sh '$HERE/elf-target.sh' '$W/host.o')\" = X86_64 ]"
check "elf-target: forged object is K1OM"   sh -c "sh '$HERE/elf-target.sh' '$W/forged-k1om.o' >/dev/null"
check "elf-target: text file is not ELF"    sh -c "! sh '$HERE/elf-target.sh' '$HERE/README.md' >/dev/null"

# 2. A host compiler disguised as a k1om cross-compiler, in a path containing
#    "k1om", must never be reported as K1OM (regression: names used to match).
mkdir -p "$W/usr-linux-k1om/bin"
printf '#!/bin/sh\nexec gcc "$@"\n' > "$W/usr-linux-k1om/bin/x86_64-k1om-linux-gcc"
chmod +x "$W/usr-linux-k1om/bin/x86_64-k1om-linux-gcc"
KNC_CROSS_CC="$W/usr-linux-k1om/bin/x86_64-k1om-linux-gcc" sh "$HERE/probe-native.sh" "$W/probe-k1om" > "$W/probe.out" 2>&1
check "probe: cross-compiler candidate was probed"  grep -q '^##### k1om cross-compiler candidate' "$W/probe.out"
check "probe: host gcc object built"                grep -q 'BUILD_VERDICT: PASS (obj produced)' "$W/probe.out"
check "probe: no false K1OM_CONFIRMED"              sh -c "! grep -q 'TARGET_VERDICT: K1OM_CONFIRMED' '$W/probe.out'"
check "probe: OpenMP link probe ran"                grep -q 'k1om-cross-1-omp-exe' "$W/probe.out"
check "probe: vectorization verdict reported"       grep -q 'VECTOR_VERDICT: .* for k1om-cross-1-vec' "$W/probe.out"
check "probe: __MIC__ verdict reported"             grep -q 'MACRO_VERDICT' "$W/probe.out"

# 3. Inventory lists every tool (dash's `command -v a b` only reports the first)
sh "$HERE/collect-toolchain.sh" "$W/inv" > /dev/null 2>&1
check "inventory: one line per tool"   sh -c "[ \$(grep -c -E '^(gcc|clang|icc|readelf|micctrl) ' '$W/inv/path.txt') -eq 5 ]"
check "inventory: gcc found"           grep -q '^gcc  *[/]' "$W/inv/path.txt"
check "inventory: k1om search ran"     grep -q 'exit 0' "$W/inv/k1om-cross-compilers.txt"

echo "selftest: $fails failure(s); work dir $W"
[ "$fails" -eq 0 ]
