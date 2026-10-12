# QEMU/KVM passthrough of the Xeon Phi 5110P to an MPSS guest

**Operator document. Nothing here is automated, and nothing here has been run
for this project.** Each command is labeled **[host]** (the modern lab
machine), **[guest]** (the RHEL/CentOS 7 MPSS VM) or **[card]** (the
coprocessor's own Linux, reached as `ssh mic0` from the guest). The repository
contains no script that binds devices to VFIO, unloads drivers, flashes the
card or edits boot configuration, and it must not gain one.

Status labels follow `docs/mpss-compatibility.md`: SOURCED / BELIEF /
UNVERIFIED. Generic Linux VFIO steps are standard kernel documentation
(<https://docs.kernel.org/driver-api/vfio.html>). Their **KNC-specific**
behavior is UNVERIFIED.

> **Working VFIO attachment ≠ working MPSS.** Seeing the card in the guest's
> `lspci` proves only that PCI passthrough works. MPSS must then load its
> modules, boot the card's uOS over PCIe, and bring up the virtual network for
> `ssh mic0`. Each step can fail independently. The only public passthrough
> report found shows the card bound to vfio-pci in a q35 Proxmox VM, with
> **no** confirmation that MPSS then worked
> (<https://linustechtips.com/topic/1300492-proxmox-xeon-phi-passthrough/>).
> Treat KNC-in-KVM as an experiment until `micctrl -s` says `online` and
> `miccheck` passes.

## 0. Physical prerequisites

- **Cooling.** The 5110P is passively cooled (225 W TDP, SOURCED). It needs
  chassis airflow through its heatsink, from a server with directed airflow or
  a duct plus a fan. Without it the card throttles or shuts down. Watch the
  card's temperature (`micinfo` or `micsmc`; field and tool names UNVERIFIED)
  during the first sweep, and abort if it climbs steadily.
- **Power.** 225 W card: use the PCIe auxiliary power connectors (2×4 / 2×3
  pin on most 5110P boards: BELIEF; check the card) and a PSU with headroom.
- **Slot.** PCIe x16 Gen2 electrical (BELIEF). Prefer a slot whose IOMMU group
  contains only the card.

## 1. Host firmware and kernel

| step | command / setting | notes |
|------|-------------------|-------|
| VT-x and VT-d on | BIOS/UEFI setup | **[host]** both required |
| Above-4G decoding / 64-bit MMIO | BIOS/UEFI setup | **[host]** BELIEF: needed for the 8 GB 64-bit prefetchable BAR0 (SOURCED BAR size). Without it the firmware may not assign BAR0 and the card does not work, even on bare metal. |
| IOMMU on | kernel cmdline `intel_iommu=on iommu=pt` | **[host]** operator edits the bootloader config; this repo never does |
| verify | `dmesg \| grep -i -e DMAR -e IOMMU` | **[host]** expect "IOMMU enabled" / DMAR remapping lines |

## 2. Identify the card and its IOMMU group

```sh
# [host] find the card: vendor 8086, device 2250 (SOURCED ID)
lspci -nn | grep -i 8086:2250
# [host] where it sits in the PCI tree (bridge, link)
lspci -tv
# [host] BARs and current driver; expect "Region 0: Memory at ... (64-bit, prefetchable) [size=8G]"
sudo lspci -vvv -s <BDF>
# [host] list IOMMU groups; the card's group should contain only the card
for g in /sys/kernel/iommu_groups/*; do echo "group ${g##*/}:"; for d in $g/devices/*; do echo "  $(lspci -nns ${d##*/})"; done; done
```

If the group contains other devices (other than PCIe root ports or bridges), pick
another slot. Do not use ACS override patches on a lab machine without
understanding the isolation loss.

## 3. Inspect (not perform) VFIO binding

The operator binds the card to `vfio-pci`, using their distribution's
supported method (e.g. `vfio-pci.ids=8086:2250` on the kernel command line, or
a modprobe.d `options vfio-pci ids=...` file). Check the result with:

```sh
# [host] which driver owns the card ("Kernel driver in use: vfio-pci" expected)
lspci -nnk -s <BDF>
# [host] vfio device node for the group exists
ls -l /dev/vfio/
```

The host must **not** load MPSS's own `mic` host driver for this card. The
guest owns it.

## 4. QEMU / libvirt guest

- Machine type: `q35` (PCIe). Firmware: SeaBIOS or OVMF. EL7-era guests boot
  with either (BELIEF).
- Device: `-device vfio-pci,host=<BDF>` (or the libvirt `<hostdev>`
  equivalent).
- **64-bit MMIO window.** The guest firmware must place an 8 GB BAR. QEMU's
  q35 64-bit PCI hole size can be raised with
  `-global q35-pcihost.pci-hole64-size=<size>` (e.g. `64G`). With OVMF, the
  equivalent knob is the `X-PciMmio64Mb` fw_cfg option
  (`-fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=65536`). Both are documented for
  large-BAR GPUs; **for KNC they are UNVERIFIED.** Symptom of a too-small
  window: BAR0 unassigned in guest `lspci -vvv`, or MPSS failing to map card
  memory.
- Guest RAM: MPSS keeps host-side buffers and the card's boot image in guest
  memory. Give the guest ≥ 8 GB (BELIEF). Pin it with hugepages if
  measurements look noisy.
- vCPUs: enough for the guest-side work. Native runs execute on the card; the
  guest only orchestrates.

## 5. Guest bring-up

See `docs/mpss-compatibility.md` for versions and sources. In order:

1. **[guest]** Install CentOS/RHEL **7.3** (kernel 3.10.0-514, matching
   MPSS 3.8.6's list). Disable kernel updates (`exclude=kernel*` in yum.conf).
2. **[guest]** `lspci -nn | grep 8086:2250`; `lspci -vvv` shows BAR0 assigned.
3. **[guest]** Install MPSS 3.8.6 following its readme (archive:
   <https://archive.org/details/intel-mpss-3.8.6>).
4. **[guest]** `micctrl --initdefaults` (UNVERIFIED for 3.8.6; check the
   readme), then `service mpss start`.
5. **[guest]** `micinfo`: record Flash and SMC versions. If they don't match
   the MPSS release, the **operator** follows the readme's flash procedure
   (`micflash`). This repo never flashes.
6. **[guest]** `micctrl -s` → `mic0: online`; `miccheck`; `ssh mic0 uname -a`
   (expect `k1om`, BELIEF).
7. Only now: Milestone 2 runs (`docs/status.md`).

## 6. Recovery and risk

- **Snapshot the guest disk** after step 3 and after step 6. An end-of-life
  guest OS plus kernel-module drivers is fragile.
- If the guest hangs while MPSS boots the card, stop the guest from the host
  (`virsh destroy`). A card left in a bad state may need a **[host]** power
  cycle (cold boot), not just a guest reboot. Device resets through VFIO may
  not work for this card (UNVERIFIED).
- A failed flash can leave the card unusable. Flash only with the image that
  matches the installed MPSS, on stable power, after recording `micinfo`.
- **End-of-life guest risk.** CentOS 7.3 gets no security updates. Keep the
  guest an isolated research appliance: no inbound network from untrusted
  networks, no shared credentials, data moved in and out via a controlled
  share or `scp` from the host.
- The lab host stays modern and patched. Only the guest is old.
