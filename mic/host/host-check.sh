#!/bin/sh
# Read-only first-boot check for the bare-metal lab host (docs/host-build.md).
#                                                               *** UNVERIFIED ***
# Collects what decides whether the Xeon Phi 5110P can be passed through to the
# MPSS VM and whether host benchmark numbers will be stable, and prints one
# verdict line per check: PASS / WARN / FAIL / INFO.
#
#   sh mic/host/host-check.sh [OUTDIR]          (root gives more detail: lspci link
#                                                 status, dmidecode memory layout)
# Output: OUTDIR (default results/hw/host-<UTC timestamp>/) with raw logs and
# SUMMARY.txt. Exit status: 1 if any FAIL, else 0.
#
# It changes nothing: no driver binding, no module loading, no BIOS or
# kernel-parameter changes, no frequency or power settings.
set -u

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT=${1:-results/hw/host-$STAMP}
mkdir -p "$OUT" || exit 2
SUMMARY="$OUT/SUMMARY.txt"
: > "$SUMMARY"
fails=0

say() { printf '%s\n' "$*" | tee -a "$SUMMARY"; }
verdict() {  # verdict LEVEL CHECK MESSAGE
    printf '%-5s %-22s %s\n' "$1" "$2" "$3" | tee -a "$SUMMARY"
    [ "$1" = FAIL ] && fails=$((fails + 1))
    return 0
}
log() {  # log NAME COMMAND... : save output of a command, never fail
    name=$1; shift
    { printf '$'; for a do printf ' %s' "$a"; done; printf '\n'; "$@" 2>&1; printf '[exit %s]\n' "$?"; } > "$OUT/$name.txt"
    return 0
}
have() { command -v "$1" >/dev/null 2>&1; }

say "knc-rf-lab host check $(date -u +%Y-%m-%dT%H:%M:%SZ)  output: $OUT"
say "user: $(id -un 2>/dev/null) (root gives lspci capabilities and dmidecode)"
say ""

# ---------------------------------------------------------------- raw logs
log uname         uname -a
log os-release    cat /etc/os-release
log cmdline       cat /proc/cmdline
log lscpu         lscpu
log cpuinfo-head  sh -c 'head -30 /proc/cpuinfo'
log meminfo       sh -c 'head -5 /proc/meminfo'
have numactl  && log numactl numactl -H
have lspci    && log lspci-tree lspci -tv
have lspci    && log lspci-nn   lspci -nn
have dmidecode && log dmidecode-memory dmidecode -t memory
have dmidecode && log dmidecode-board  dmidecode -t baseboard -t bios
log dmesg-iommu   sh -c 'dmesg 2>/dev/null | grep -i -e DMAR -e IOMMU -e "AMD-Vi" | head -40'
log iommu-groups  sh -c 'for g in /sys/kernel/iommu_groups/*; do [ -d "$g" ] || continue; for d in "$g"/devices/*; do printf "group %s: %s\n" "${g##*/}" "$(lspci -nns "${d##*/}" 2>/dev/null || echo "${d##*/}")"; done; done'
have qemu-system-x86_64 && log qemu-version qemu-system-x86_64 --version
have virsh && log libvirt-version virsh --version
log microcode     sh -c 'grep -m1 microcode /proc/cpuinfo'
log rapl          sh -c 'for z in /sys/class/powercap/intel-rapl:*; do [ -d "$z" ] || continue; printf "%s %s limit0_uw=%s limit1_uw=%s\n" "${z##*/}" "$(cat "$z/name" 2>/dev/null)" "$(cat "$z/constraint_0_power_limit_uw" 2>/dev/null)" "$(cat "$z/constraint_1_power_limit_uw" 2>/dev/null)"; done'

# ---------------------------------------------------------------- verdicts
# Bare metal
if grep -q ' hypervisor' /proc/cpuinfo; then
    verdict WARN bare-metal "hypervisor CPU flag set: this is a VM, not the lab host"
else
    verdict PASS bare-metal "no hypervisor flag"
fi

# CPUs, sockets, NUMA
model=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo | head -1)
sockets=$(lscpu 2>/dev/null | sed -n 's/^Socket(s):[[:space:]]*//p')
nodes=$(lscpu 2>/dev/null | sed -n 's/^NUMA node(s):[[:space:]]*//p')
verdict INFO cpu "${model:-unknown}; sockets=${sockets:-?} numa_nodes=${nodes:-?} cpus=$(getconf _NPROCESSORS_ONLN)"
if grep -q avx512f /proc/cpuinfo; then
    verdict INFO avx512 "AVX-512 available: host baseline is vectorized; label comparisons with scalar KNC code"
fi
[ "${sockets:-1}" -ge 2 ] && ! have numactl && verdict WARN numactl "numactl missing: per-socket host baselines need it"

# Virtualization and IOMMU
if grep -qw vmx /proc/cpuinfo; then
    verdict PASS vt-x "vmx flag present"
else
    verdict FAIL vt-x "no vmx flag: enable Intel VT-x in the BIOS"
fi
if grep -q 'intel_iommu=on' /proc/cmdline; then
    verdict PASS iommu-cmdline "intel_iommu=on on the kernel command line"
else
    verdict WARN iommu-cmdline "intel_iommu=on not on the kernel command line (operator adds it; some kernels enable it by default)"
fi
ngroups=$(ls /sys/kernel/iommu_groups 2>/dev/null | wc -l)
if [ "$ngroups" -gt 0 ]; then
    verdict PASS iommu-active "$ngroups IOMMU groups"
else
    verdict FAIL iommu-active "no IOMMU groups: enable VT-d in the BIOS and intel_iommu=on"
fi
[ -e /dev/kvm ] && verdict PASS kvm "/dev/kvm present" || verdict FAIL kvm "/dev/kvm missing (kvm_intel module / VT-x)"

# The card
if ! have lspci; then
    verdict FAIL card "lspci not installed (pciutils)"
else
    cards=$(lspci -D -d 8086:2250 2>/dev/null | cut -d' ' -f1)
    if [ -z "$cards" ]; then
        verdict FAIL card "no Xeon Phi 8086:2250 found (seated? powered? slot enabled?)"
    fi
    for d in $cards; do
        lspci -vvv -s "$d" > "$OUT/card-$d.txt" 2>&1
        verdict PASS card "$d $(lspci -s "$d" | cut -d' ' -f2-)"
        bar=$(grep -m1 'Region 0:' "$OUT/card-$d.txt")
        case "$bar" in
            *unassigned*|*disabled*|"") verdict FAIL card-bar0 "$d BAR0 not assigned: enable Above 4G Decoding (docs/qemu-passthrough.md)" ;;
            *) verdict PASS card-bar0 "$d $(echo "$bar" | sed 's/^[[:space:]]*//')" ;;
        esac
        sta=$(sed -n 's/^[[:space:]]*LnkSta:[[:space:]]*//p' "$OUT/card-$d.txt" | head -1)
        cap=$(sed -n 's/^[[:space:]]*LnkCap:[[:space:]]*//p' "$OUT/card-$d.txt" | head -1)
        if [ -z "$sta" ]; then
            verdict INFO card-link "$d link status needs root (lspci -vvv)"
        else
            case "$sta" in
                *"Width x16"*) verdict PASS card-link "$d $sta" ;;
                *) verdict WARN card-link "$d $sta (capable: $cap); x16 slot wired to a CPU expected" ;;
            esac
        fi
        node=$(cat "/sys/bus/pci/devices/$d/numa_node" 2>/dev/null || echo "?")
        verdict INFO card-numa "$d is on NUMA node $node: pin the MPSS VM's vCPUs and memory there"
        grp=$(basename "$(readlink "/sys/bus/pci/devices/$d/iommu_group" 2>/dev/null)" 2>/dev/null)
        if [ -n "$grp" ] && [ -d "/sys/kernel/iommu_groups/$grp" ]; then
            others=""
            for o in "/sys/kernel/iommu_groups/$grp/devices"/*; do
                [ "${o##*/}" = "$d" ] || others="$others${o##*/} "
            done
            if [ -z "$others" ]; then
                verdict PASS card-iommu "$d alone in IOMMU group $grp"
            else
                verdict WARN card-iommu "$d shares IOMMU group $grp with: $others(bridges are fine; endpoints must also be passed through or the slot changed)"
            fi
        else
            verdict FAIL card-iommu "$d has no IOMMU group"
        fi
        drv=$(basename "$(readlink "/sys/bus/pci/devices/$d/driver" 2>/dev/null)" 2>/dev/null)
        verdict INFO card-driver "$d bound to: ${drv:-none} (vfio-pci binding is an operator step)"
    done
fi

# Benchmark stability
govs=$(cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor 2>/dev/null | sort | uniq -c | tr -s ' ' | tr '\n' ';')
if [ -z "$govs" ]; then
    verdict INFO governor "no cpufreq interface (BIOS-controlled or VM)"
else
    case "$govs" in
        *performance*) verdict PASS governor "$govs" ;;
        *) verdict WARN governor "$govs set 'performance' for benchmark runs (operator)" ;;
    esac
fi
if [ -r /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    verdict INFO turbo "intel_pstate no_turbo=$(cat /sys/devices/system/cpu/intel_pstate/no_turbo) (record it with every host run)"
elif [ -r /sys/devices/system/cpu/cpufreq/boost ]; then
    verdict INFO turbo "cpufreq boost=$(cat /sys/devices/system/cpu/cpufreq/boost)"
else
    verdict INFO turbo "turbo state not readable"
fi
[ -r /sys/devices/system/cpu/smt/active ] && verdict INFO smt "smt active=$(cat /sys/devices/system/cpu/smt/active)"
rapl=$(grep -c 'package' "$OUT/rapl.txt" 2>/dev/null)
if [ "${rapl:-0}" -gt 0 ]; then
    verdict INFO cpu-power "package power limits (W): $(grep package "$OUT/rapl.txt" | sed -n 's/.*limit0_uw=\([0-9]*\).*/\1/p' | awk '{printf "%.0f ", $1/1e6}') (compare with the board's 205 W rating, docs/host-build.md)"
else
    verdict INFO cpu-power "RAPL not readable"
fi

# Memory population (needs root + dmidecode)
if [ -s "$OUT/dmidecode-memory.txt" ] && grep -q 'Memory Device' "$OUT/dmidecode-memory.txt"; then
    pop=$(grep -E '^[[:space:]]+Size: [0-9]' "$OUT/dmidecode-memory.txt" | wc -l)
    empty=$(grep -E '^[[:space:]]+Size: No Module' "$OUT/dmidecode-memory.txt" | wc -l)
    speed=$(sed -n 's/^[[:space:]]*Configured Memory Speed: //p' "$OUT/dmidecode-memory.txt" | sort | uniq -c | tr -s ' ' | tr '\n' ';')
    if [ "${sockets:-1}" -ge 2 ] && [ "$pop" -lt 12 ]; then
        verdict WARN memory "$pop DIMMs populated, $empty empty; 12 (one per channel, 6 per socket) needed for full bandwidth; speed: $speed"
    else
        verdict PASS memory "$pop DIMMs populated, $empty empty; speed: $speed"
    fi
else
    verdict INFO memory "DIMM layout needs root and dmidecode"
fi

# Virtualization stack
have qemu-system-x86_64 && verdict INFO qemu "$(qemu-system-x86_64 --version | head -1)" || verdict WARN qemu "qemu-system-x86_64 not installed"
have virsh && verdict INFO libvirt "virsh $(virsh --version 2>/dev/null)" || verdict WARN libvirt "libvirt (virsh) not installed"

say ""
say "$fails FAIL(s). Raw logs in $OUT. Paste SUMMARY.txt into the issue or chat."
[ "$fails" -eq 0 ]
