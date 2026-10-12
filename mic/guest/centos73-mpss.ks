# Kickstart: CentOS 7.3 (1611) guest for Intel MPSS 3.8.6.       *** UNVERIFIED ***
# Never run. The vault URL could not be reached from the authoring environment.
#
# Goal: a minimal, kernel-locked EL7.3 install (kernel 3.10.0-514.el7, the
# kernel MPSS 3.8.6 lists for RHEL 7.3; see docs/mpss-compatibility.md) with a
# build toolchain. It does NOT install MPSS. That is mic/guest/install-mpss-guest.sh,
# run by the operator after first boot.
#
# Use, e.g. (operator, on the host):
#   virt-install --name knc-mpss-install --memory 16384 --vcpus 8 --machine q35 \
#     --disk path=@DISK_PATH@,size=60,format=qcow2,bus=virtio \
#     --location https://vault.centos.org/7.3.1611/os/x86_64/ \
#     --initrd-inject mic/guest/centos73-mpss.ks --extra-args "inst.ks=file:/centos73-mpss.ks console=ttyS0" \
#     --network network=default --graphics none
# Then define the passthrough domain from mic/guest/knc-mpss-guest.xml on the same disk.
#
# Change before use: the root password and user password hashes below are
# placeholders that LOCK the accounts. Generate real ones with
#   python3 -c 'import crypt; print(crypt.crypt("...", crypt.mksalt(crypt.METHOD_SHA512)))'

text
skipx
firstboot --disable
reboot
lang en_US.UTF-8
keyboard us
timezone UTC --isUtc
eula --agreed

# Install tree pinned to 7.3.1611 (no "updates" repo: updates would move the kernel).
url --url="https://vault.centos.org/7.3.1611/os/x86_64/"

network --bootproto=dhcp --device=link --activate --hostname=knc-mpss
firewall --enabled --service=ssh
selinux --enforcing
services --enabled=sshd,chronyd

# Placeholders: "!!" = locked password. Replace before use.
rootpw --iscrypted !!
user --name=knc --groups=wheel --iscrypted --password=!!

bootloader --location=mbr --boot-drive=vda --append="console=ttyS0,115200 console=tty0"
zerombr
clearpart --all --initlabel --drives=vda
autopart --type=lvm

%packages
@core
@development
kernel-devel
kernel-headers
openssh-clients
rsync
bridge-utils
pciutils
usbutils
chrony
-NetworkManager-wifi
%end

%post --log=/root/ks-post.log
set -x
# Lock the kernel: MPSS 3.8.x host modules are built for specific kernels.
echo 'exclude=kernel* kernel-devel* kernel-headers*' >> /etc/yum.conf
# Point base repos at the 7.3.1611 vault (the default mirrorlist serves 7.9).
sed -i -e 's/^mirrorlist=/#mirrorlist=/' \
       -e 's|^#baseurl=http://mirror.centos.org/centos/$releasever|baseurl=https://vault.centos.org/7.3.1611|' \
       /etc/yum.repos.d/CentOS-Base.repo
# Record what was installed, for docs/status.md evidence.
{ uname -r; rpm -q kernel kernel-devel kernel-headers gcc; cat /etc/centos-release; } > /root/ks-installed-versions.txt
%end
