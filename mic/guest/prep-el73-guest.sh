#!/bin/sh
# Turn a hand-installed CentOS 7.3.1611 guest (e.g. from the LiveKDE or Minimal
# ISO, where the kickstart's package list does not apply) into the state that
# centos73-mpss*.ks produces.                                     *** UNVERIFIED ***
#
# Run as root in the guest, once, before install-mpss-guest.sh:
#   mic/guest/prep-el73-guest.sh
#
# It: checks the release and the 7.3 GA kernel, locks kernel packages in
# /etc/yum.conf, points the base repo at the 7.3.1611 vault (or a local DVD
# ISO), installs the build toolchain and kernel-devel for the running kernel,
# and records versions. It changes no boot configuration; set NO_GUI=1 to also
# make the text console the default target (KDE stays installed).
#
# Environment:
#   EXPECT_KERNEL  default 3.10.0-514.el7.x86_64
#   DVD_ISO        optional path to CentOS-7-x86_64-DVD-1611.iso to use as the
#                  package source instead of the network vault
#   NO_GUI=1       systemctl set-default multi-user.target
set -eu
EXPECT_KERNEL=${EXPECT_KERNEL:-3.10.0-514.el7.x86_64}
die() { echo "prep-el73-guest: ERROR: $*" >&2; exit 1; }
note() { echo "prep-el73-guest: $*"; }

[ "$(id -u)" -eq 0 ] || die "run as root"
grep -q '7\.3\.1611' /etc/centos-release || die "not CentOS 7.3.1611: $(cat /etc/centos-release)"
[ "$(uname -r)" = "$EXPECT_KERNEL" ] || die "running kernel $(uname -r), expected $EXPECT_KERNEL"

# 1. Lock kernel packages (MPSS host modules are built per kernel).
grep -q '^exclude=.*kernel' /etc/yum.conf || echo 'exclude=kernel* kernel-devel* kernel-headers*' >> /etc/yum.conf

# 2. Package source: never the live mirrors (they serve 7.9 and would upgrade glibc etc.).
mkdir -p /etc/yum.repos.d/disabled
for f in /etc/yum.repos.d/CentOS-*.repo; do
    [ -e "$f" ] && mv "$f" /etc/yum.repos.d/disabled/
done
if [ -n "${DVD_ISO:-}" ]; then
    [ -f "$DVD_ISO" ] || die "DVD_ISO=$DVD_ISO not found"
    mkdir -p /mnt/c73dvd
    mountpoint -q /mnt/c73dvd || mount -o loop,ro "$DVD_ISO" /mnt/c73dvd
    cat > /etc/yum.repos.d/c73-local.repo <<'REPO'
[c73-dvd]
name=CentOS 7.3.1611 DVD (local)
baseurl=file:///mnt/c73dvd
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7
enabled=1
REPO
else
    cat > /etc/yum.repos.d/c73-vault.repo <<'REPO'
[c73-vault-os]
name=CentOS 7.3.1611 os (vault)
baseurl=http://vault.centos.org/7.3.1611/os/x86_64/
gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-CentOS-7
enabled=1
REPO
fi
yum clean all >/dev/null

# 3. Toolchain + headers for the running kernel (exact version, not "latest").
KV=$(uname -r | sed 's/\.x86_64$//')
yum -y install gcc gcc-c++ make perl pciutils openssh-clients rsync \
    "kernel-devel-$KV" "kernel-headers-$KV" \
    || die "package install failed (is the 7.3.1611 vault or DVD reachable?)"

# 4. Optional: boot to text console by default.
if [ "${NO_GUI:-0}" = 1 ]; then
    systemctl set-default multi-user.target
    note "default target set to multi-user (KDE still installed; 'systemctl isolate graphical.target' to use it)"
fi

{ uname -r; rpm -q kernel kernel-devel kernel-headers gcc; cat /etc/centos-release; } \
    | tee /root/ks-installed-versions.txt
note "done; next: MPSS_TAR=... mic/guest/install-mpss-guest.sh"
