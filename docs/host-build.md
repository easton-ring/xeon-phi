# Lab host build: dual Xeon Platinum 8175M on a Gigabyte MD71-HB0

The bare-metal host has two jobs: it runs the host benchmark baseline
(Milestone 2, step 20) and it hosts the MPSS VM with the Xeon Phi 5110P
passed through (`docs/qemu-passthrough.md`). Nothing here has been built or
tested yet.

Labels follow `docs/mpss-compatibility.md`: **SOURCED** (URL given; read as
search excerpts, the Gigabyte manual PDF could not be downloaded from the
authoring environment), **BELIEF**, **UNVERIFIED**.

## 1. Resolve first: 8175M on this board

| item | status | detail |
|------|--------|--------|
| Board CPU power rating | SOURCED (retailers) | MD71-HB0: dual LGA3647, Intel C622, "TDP 205W max" per socket (<https://www.serverdirect.nl/en/components/motherboards/intel/2-processors/md71-hb0/33725>). Check the CPU support list and manual on Gigabyte's page (<https://www.gigabyte.com/Server-Motherboard/MD71-HB0-rev-1x>). |
| 8175M TDP | SOURCED (sellers only) | 240 W per most listings, one says 165 W. It is an OEM (AWS) part with no Intel ARK page, so there is no official figure. |
| 8175M on the MD71-HB0 | **UNVERIFIED** | No source found for this pairing. OEM parts are not on retail CPU support lists. |
| 8175M on other boards | SOURCED (anecdotal, both directions) | Works on a Microsoft MtOlympus C2012 with a Skylake-only BIOS, "240W TDP / 255A TDC is no problem" (<https://forums.servethehome.com/index.php?threads/xeon-8259cl.33334/>). Boot halt with a catastrophic error on a Supermicro X11SPi-TF; the poster suspected an ME-region check (<https://winraid.level1techs.com/t/asus-c621e-sage-and-modern-xeons-like-8170-8280-8222cl-8259cl-8171m-etc/37502>). |
| Board status | SOURCED | Gigabyte lists the MD71-HB0 as end of support (product page above), so expect no new BIOS releases. |
| ServeTheHome guide for running the 8175M | **to add** | The owner reports an STH forum guide that explains exactly how to get the 8175M running. Add its URL and the steps (BIOS version, settings, power limits) here before buying. |

What a 240 W CPU on a 205 W-rated board can mean (BELIEF, to be checked
against the guide and the board): the BIOS may not boot it at all, or it boots
and the firmware caps the package power. The host check records the package
power limit the BIOS applies (RAPL) in `cpu-power`, and a lower cap shows up as
lower all-core clocks in the host baseline. A capped run is still a valid
baseline as long as the cap is recorded.

## 2. Parts checklist

| part | requirement | why |
|------|-------------|-----|
| Board | MD71-HB0: confirm in the manual that the BIOS has **VT-d**, **Above 4G Decoding** and SR-IOV/ARI options; identify which PCIe slots are wired to CPU0 vs CPU1 (block diagram) | passthrough needs VT-d; the 5110P's 8 GB BAR0 needs Above 4G decoding (`docs/qemu-passthrough.md`) |
| Slot for the 5110P | electrically **x16**, attached to a populated CPU, room for a full-length double-width card | a narrower link slows host↔card transfers (offload, deploy) |
| CPUs | 2× 8175M, only after §1 is resolved | |
| Memory | DDR4 ECC RDIMM, **one DIMM in each of the 6 channels per socket (12 DIMMs)**, same size and speed; the board has 16 slots, so follow the manual's population order. 64–128 GB total | an unbalanced or partly filled layout starves the host baseline of memory bandwidth |
| Cooling (card) | directed front-to-back airflow through the 5110P's heatsink: server chassis or a shroud plus a high-static-pressure fan | the 5110P is passive, 225 W |
| Cooling (CPUs) | LGA3647 coolers for the board's narrow/square ILM type (check the manual), rated for the CPUs' real power | |
| Power supply | 2 × CPU power + 225 W card + rest; roughly 1000 W or more of quality PSU, with the PCIe auxiliary connectors the card needs and both CPU EPS connectors | |
| Management | the board's BMC (remote console, power cycle) on the network | resetting a wedged card needs a power cycle; essential for `docs/kernel-port.md` work |
| Storage | an SSD for the host OS and VM images (60 GB per MPSS VM) | |
| Host OS | a current Linux with KVM, QEMU, libvirt, pciutils, numactl, dmidecode | |

## 3. BIOS settings (operator)

- Intel VT-x and VT-d: on.
- Above 4G Decoding (64-bit MMIO): on.
- SR-IOV / ARI: on if offered (harmless; some boards tie large-BAR handling to them).
- CPU power settings: per the ServeTheHome guide (§1).
- For benchmark runs: note the turbo setting; do not change it between host runs.

Kernel command line (operator, bootloader config): `intel_iommu=on iommu=pt`.

## 4. First boot: run the host check

```sh
sudo sh mic/host/host-check.sh
```

Read-only. It writes `results/hw/host-<timestamp>/` with raw logs and
`SUMMARY.txt` (PASS / WARN / FAIL / INFO lines) and exits 1 on any FAIL:

| check | passes when |
|-------|-------------|
| `bare-metal` | no hypervisor flag |
| `vt-x`, `kvm` | VT-x on, `/dev/kvm` present |
| `iommu-cmdline`, `iommu-active` | `intel_iommu=on`, IOMMU groups exist |
| `card`, `card-bar0` | 5110P (8086:2250) found, BAR0 assigned (8 GB expected) |
| `card-link` | link width x16 (needs root) |
| `card-iommu` | the card is alone in its IOMMU group |
| `card-numa` | INFO: the NUMA node to pin the MPSS VM to |
| `governor`, `turbo`, `smt`, `cpu-power` | recorded; governor `performance` for benchmarks; package power limits shown in watts |
| `memory` | 12 DIMMs populated on a 2-socket system (needs root + dmidecode) |
| `qemu`, `libvirt` | installed |

Paste `SUMMARY.txt` back for review.

## 5. Using two sockets well

- **MPSS VM:** pin its vCPUs and memory to the card's NUMA node (`card-numa`),
  e.g. libvirt `<vcpupin>` / `<numatune>`; otherwise every PCIe transfer
  crosses the socket interconnect.
- **Host baseline:** run per socket (`numactl --cpunodebind=N --membind=N`)
  and across both, with thread points at the socket boundaries (24 cores per
  socket, 48 cores total, 96 with SMT), and record governor, turbo and power
  limit with the results. `knc_bench` already records the thread team and
  OpenMP environment; NUMA binding is set from the outside.
- **AVX-512:** the host build uses `-march=native`; gcc may still prefer
  256-bit vectors (`-mprefer-vector-width=512` changes that). Without icc,
  card results are scalar gcc-k1om code (`docs/status.md`): label every
  comparison.
