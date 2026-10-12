#!/bin/sh
# Install Intel MPSS 3.8.6 inside the CentOS 7.3 guest and bring the card online.
#                                                               *** UNVERIFIED ***
# Never run against a real card. MPSS package layout and command names follow
# MPSS 3.x readmes (docs/mpss-compatibility.md); READ THE 3.8.6 README IN THE
# TARBALL FIRST and adjust this script if it disagrees.
#
# Run as root IN THE GUEST, after the card has been passed through:
#   MPSS_TAR=/root/mpss-3.8.6-linux.tar [MPSS_SHA256=<hex>] mic/guest/install-mpss-guest.sh
#
# What it does: pre-flight checks -> extract -> yum localinstall the RPMs ->
#   micctrl --initdefaults -> start the mpss service -> wait for "online" ->
#   collect-diagnostics.sh.
# What it NEVER does: flash the card (micflash), change the kernel or boot
#   configuration, touch the host, or bind/unbind PCI drivers. If the card's
#   flash does not match MPSS it stops and tells the operator.
#
# Environment:
#   MPSS_TAR       path to the MPSS 3.8.6 Linux tarball (from https://archive.org/details/intel-mpss-3.8.6)
#   MPSS_SHA256    optional expected SHA-256 of the tarball
#   EXPECT_KERNEL  kernel the MPSS modules must match (default 3.10.0-514.el7.x86_64)
#   YES=1          do not prompt before installing
set -eu

EXPECT_KERNEL=${EXPECT_KERNEL:-3.10.0-514.el7.x86_64}
MPSS_TAR=${MPSS_TAR:?set MPSS_TAR to the mpss-3.8.6 Linux tarball}
SRC=/opt/mpss-src
HERE=$(cd "$(dirname "$0")" && pwd)

die() { echo "install-mpss-guest: ERROR: $*" >&2; exit 1; }
note() { echo "install-mpss-guest: $*"; }

# ---- pre-flight -------------------------------------------------------------
[ "$(id -u)" -eq 0 ] || die "run as root"
grep -q ' 7\.3' /etc/centos-release 2>/dev/null || grep -q ' 7\.3' /etc/redhat-release 2>/dev/null \
    || note "WARNING: not EL 7.3 ($(cat /etc/*-release 2>/dev/null | head -1)); MPSS 3.8.6 lists RHEL 7.1-7.3"
[ "$(uname -r)" = "$EXPECT_KERNEL" ] \
    || die "running kernel $(uname -r) != $EXPECT_KERNEL. MPSS host modules must match the kernel; boot the 7.3 GA kernel or rebuild the modules from the MPSS source per its readme."
lspci -nn | grep -qi '8086:2250' \
    || die "no Xeon Phi (8086:2250) visible in this guest: fix PCI passthrough first (docs/qemu-passthrough.md)"
lspci -vvv -d 8086:2250 | grep -q 'Region 0: Memory at' \
    || die "card BAR0 is not assigned in the guest (64-bit MMIO window too small?) - see docs/qemu-passthrough.md section 4"
[ -f "$MPSS_TAR" ] || die "$MPSS_TAR not found"
if [ -n "${MPSS_SHA256:-}" ]; then
    echo "$MPSS_SHA256  $MPSS_TAR" | sha256sum -c - || die "tarball SHA-256 mismatch"
fi
note "tarball sha256: $(sha256sum "$MPSS_TAR" | cut -d' ' -f1)"

# ---- extract and inspect ----------------------------------------------------
mkdir -p "$SRC"
tar -xf "$MPSS_TAR" -C "$SRC"
RPMDIR=$(dirname "$(find "$SRC" -name 'mpss-daemon*.rpm' | head -1)")
[ -n "$RPMDIR" ] && [ "$RPMDIR" != "." ] || die "no mpss-daemon*.rpm in the tarball; layout differs from MPSS 3.x - follow the readme by hand"
note "RPM directory: $RPMDIR"
KREL=$(uname -r | sed 's/\.x86_64$//')
found_k=0
for f in "$RPMDIR"/*.rpm; do case "$f" in *"$KREL"*) found_k=1 ;; esac; done
[ "$found_k" = 1 ] \
    || note "WARNING: no RPM in $RPMDIR names kernel $(uname -r); kernel modules may need rebuilding from $SRC (see readme)"
find "$SRC" -maxdepth 3 -iname '*readme*' -exec echo "  readme: {}" \;

if [ "${YES:-0}" != 1 ]; then
    printf 'Install all RPMs in %s with yum localinstall? [y/N] ' "$RPMDIR"
    read -r ans
    [ "$ans" = y ] || [ "$ans" = Y ] || die "aborted by operator"
fi

# ---- install ----------------------------------------------------------------
# Exclude debuginfo/devel-only packages that are not needed to run the card (names UNVERIFIED).
set --
for f in "$RPMDIR"/*.rpm; do
    case "$f" in *debuginfo*|*.src.rpm) continue ;; esac
    set -- "$@" "$f"
done
yum -y localinstall "$@"
modprobe mic || die "modprobe mic failed: kernel module does not match this kernel"

# MPSS copies the root user's SSH public key to the card at initdefaults (BELIEF).
[ -f /root/.ssh/id_rsa.pub ] || ssh-keygen -t rsa -N '' -f /root/.ssh/id_rsa
micctrl --initdefaults

# ---- flash check (report only; never flash) --------------------------------
micinfo > /root/micinfo-before-start.txt 2>&1 || true
note "flash/SMC versions (compare with the image in /usr/share/mpss/flash or /opt/intel/mic/flash):"
grep -i -e 'Flash Version' -e 'SMC Firmware' /root/micinfo-before-start.txt || note "  (not reported before mpss start)"
ls /usr/share/mpss/flash /opt/intel/mic/flash 2>/dev/null || true

# ---- start and wait ---------------------------------------------------------
systemctl start mpss 2>/dev/null || service mpss start
note "waiting for mic0 to come online (up to 300 s)"
i=0
while [ $i -lt 60 ]; do
    if micctrl -s 2>/dev/null | grep -q 'online'; then break; fi
    sleep 5; i=$((i + 1))
done
micctrl -s
micctrl -s | grep -q online || die "card did not reach 'online'. Check 'micctrl -s', dmesg, /var/log/messages. If micinfo shows a flash/MPSS mismatch, the OPERATOR flashes per the MPSS readme (this script never does)."

note "card online; collecting diagnostics"
cd "$HERE/../.."
sh "$HERE/collect-diagnostics.sh"
note "next: install Intel C++ 17.0 (PSXE 2017), then docs/status.md Milestone 2 step 8"
