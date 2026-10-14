# MPSS guest VM (CentOS 7.3 + MPSS 3.8.6)                     *** UNVERIFIED ***

Files for building the research-appliance VM that owns the Xeon Phi 5110P.
**None of them has been run.** The authoring environment had no KVM, no card
and no access to vault.centos.org. Background and sources:
`docs/qemu-passthrough.md` and `docs/mpss-compatibility.md`.

| file | runs where | what it does |
|------|------------|--------------|
| `centos73-mpss.ks` | anaconda (guest install) | minimal CentOS 7.3.1611 from the vault, kernel locked at 3.10.0-514, dev tools; no MPSS. Passes `ksvalidator -v RHEL7`. |
| `centos73-mpss-dvd.ks` | anaconda (guest install) | same as above, but installs offline from `CentOS-7-x86_64-DVD-1611.iso` (`cdrom` source). Passes `ksvalidator -v RHEL7`. |
| `prep-el73-guest.sh` | **[guest]** root | for a guest installed by hand (LiveKDE/LiveGNOME/Minimal ISO, where no kickstart applies): checks 7.3.1611 + kernel 3.10.0-514, locks the kernel, switches yum to the 7.3 vault or a local DVD ISO, installs gcc / make / kernel-devel for the running kernel |
| `knc-mpss-guest.xml` | **[host]** libvirt | q35 domain, 16 GB / 8 vCPU, card as `hostdev managed='no'`, 64 GB 64-bit PCI hole. Well-formed XML (`xmllint`); not checked by `virt-xml-validate`. |
| `install-mpss-guest.sh` | **[guest]** root | pre-flight checks (kernel, card visible, BAR0 assigned) → install MPSS RPMs → `micctrl --initdefaults` → start mpss → wait for `online` → diagnostics. Never flashes. |
| `collect-diagnostics.sh` | **[guest]** root | read-only snapshot: `uname`, `lspci`, `micinfo`, `micctrl -s`, `miccheck`, card `uname -m` and cpuinfo → `results/hw/<date>/diag/` |
| `Makefile` (`make -C mic/guest check`) | **[guest]** | builds the C core plus the C tests with EL7's gcc 4.8 and runs them (the old-gcc portability check; CMake 3 and Python 3.10 are not on EL7.3) |

Both shell scripts pass `sh -n` and `shellcheck -S warning`.

## Which CentOS 7.3 ISO

From `https://vault.centos.org/7.3.1611/isos/x86_64/`:

| ISO | size | use it? |
|-----|------|---------|
| `CentOS-7-x86_64-DVD-1611.iso` | 4.1 GB | **recommended.** Full package set, so `@development` and `kernel-devel-3.10.0-514` install offline; works with `centos73-mpss-dvd.ks` |
| `CentOS-7-x86_64-LiveKDE-1611.iso` / `LiveGNOME` | 1.7 / 1.2 GB | works, but it's a desktop live image: the installer copies a fixed image and ignores kickstart package lists. Install by hand, then run `prep-el73-guest.sh` (needs the vault over the network, or the DVD ISO via `DVD_ISO=`). Ships a GUI the appliance doesn't need. |
| `CentOS-7-x86_64-Minimal-1611.iso` | 680 MB | install by hand + `prep-el73-guest.sh` (compilers come from the vault or the DVD) |
| `NetInstall`, `Everything` | | NetInstall = the network kickstart route (`centos73-mpss.ks`); Everything is unnecessary |

Verify any ISO before use, with `sha256sum.txt` and its signature
`sha256sum.txt.asc` from the same directory, and the CentOS 7 key
(`RPM-GPG-KEY-CentOS-7`, also on the ISO):

```sh
gpg --import RPM-GPG-KEY-CentOS-7
gpg --verify sha256sum.txt.asc sha256sum.txt     # "Good signature" from the CentOS-7 key
sha256sum -c sha256sum.txt --ignore-missing      # "<iso>: OK"
```

Record the ISO name and SHA-256 in `results/hw/<date>/diag/`.

## Operator sequence

Steps marked **you** are manual by design: the repository never binds
devices, edits host boot configuration or flashes the card.

1. **you [host]**: firmware VT-x / VT-d / above-4G decoding on; kernel
   `intel_iommu=on iommu=pt`; airflow over the card
   (`docs/qemu-passthrough.md` §0–2).
2. **[host]** Install the guest OS (no card attached yet):
   ```sh
   qemu-img create -f qcow2 /var/lib/libvirt/images/knc-mpss.qcow2 60G
   virt-install --name knc-mpss-install --memory 16384 --vcpus 8 --machine q35 \
     --disk path=/var/lib/libvirt/images/knc-mpss.qcow2,format=qcow2,bus=virtio \
     --location https://vault.centos.org/7.3.1611/os/x86_64/ \
     --initrd-inject mic/guest/centos73-mpss.ks \
     --extra-args "inst.ks=file:/centos73-mpss.ks console=ttyS0" \
     --network network=default --graphics none
   virsh undefine knc-mpss-install      # keeps the disk
   ```
   Edit the password placeholders (`!!` = locked) in the kickstart first. If
   the 7.3 installer cannot do TLS to the vault, use the `http://` URL.
   **With the DVD ISO** use `--location /path/CentOS-7-x86_64-DVD-1611.iso
   --initrd-inject mic/guest/centos73-mpss-dvd.ks
   --extra-args "inst.ks=file:/centos73-mpss-dvd.ks console=ttyS0"`.
   **With a Live or Minimal ISO** install interactively
   (`virt-install --cdrom <iso> ...`), then in the guest run
   `mic/guest/prep-el73-guest.sh` (add `DVD_ISO=<path>` to avoid the network,
   `NO_GUI=1` to boot to text by default).
3. **you [host]**: bind the card to vfio-pci, then check
   `lspci -nnk -d 8086:2250` shows `vfio-pci`.
4. **[host]** Fill the `@PLACEHOLDER@`s in `knc-mpss-guest.xml`, then
   `virsh define mic/guest/knc-mpss-guest.xml && virsh start knc-mpss`.
   Snapshot the disk now (`virsh snapshot-create-as knc-mpss pre-mpss`).
5. **[guest]** Copy the repo and the MPSS tarball
   (<https://archive.org/details/intel-mpss-3.8.6>) in, then:
   ```sh
   make -C mic/guest check                                   # gcc 4.8 portability
   MPSS_TAR=/root/mpss-3.8.6-linux.tar mic/guest/install-mpss-guest.sh
   ```
   If it stops on a flash/MPSS mismatch, **you** flash per the MPSS readme
   with the matching image, power-cycle as the readme says, and rerun.
6. **[guest]** Snapshot again once `micctrl -s` says `online`. Then install
   Intel C++ 17.0 (PSXE 2017; licensed media) and continue with
   `docs/status.md` Milestone 2, step 8.

## Known gaps

- MPSS 3.8.6 tarball layout, RPM names, and whether the bundled kernel
  modules match 3.10.0-514: taken from MPSS 3.x readmes, not checked against
  the 3.8.6 tarball. The script stops if `mpss-daemon*.rpm` is missing or
  `modprobe mic` fails.
- RHEL 7.3 vs 7.4 support for 3.8.6 is disputed between sources. This setup
  uses 7.3. If 7.3 fails, the documented fallback is a CentOS 7.9 guest with
  Jan Just Keijser's patched 3.8.6 modules (`docs/mpss-compatibility.md`,
  "Community driver ports").
- Whether MPSS can boot a KNC card **inside a KVM guest** at all is
  unconfirmed. No public report of it working was found.
- The guest OS is end-of-life: keep it isolated (switch the domain to an
  isolated libvirt network after setup).
