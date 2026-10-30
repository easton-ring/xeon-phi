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
| ServeTheHome method for high-current OEM CPUs | SOURCED (see §1a) | Thread "VRM modify ICC_MAX to run high Current/TDP OEM processors" (<https://forums.servethehome.com/index.php?threads/vrm-modify-icc_max-to-run-high-tdc-oem-cpu.38686/>): raise the board's VRM current limit (ICC_MAX). |

What a 240 W CPU on a 205 W-rated board can mean (BELIEF): the board may
refuse to power the CPU on, or it boots and the firmware caps the package
power. The host check records the package power limit the BIOS applies (RAPL)
in `cpu-power`, and a lower cap shows up as lower all-core clocks in the host
baseline. A capped run is still a valid baseline as long as the cap is recorded.

### 1a. The ICC_MAX modification (operator only, never scripted here)

Sources: the ServeTheHome thread above (read only as search excerpts:
`forums.servethehome.com` is not reachable from the authoring environment), and
the source code of its Python port, `stephensolis/vrm-iccmax`
(<https://github.com/stephensolis/vrm-iccmax>, read in full at commit `12e717c`,
2026-10-15). The original tool is RolloZ170's `MCP2221a_iccmax_FF`, linked in
the thread.

**What it does** (from the port's code):
- talks to the CPU VRM controllers over PMBus through an **MCP2221A USB-I2C
  adapter** connected to the board's I2C/PMBus;
- `scan` identifies controllers, read-only;
- `set` writes **ICC_MAX = 255 A** (0xFF, the register's maximum) and
  **commits it to the controller's non-volatile memory**, so it survives power
  cycles. It has a dry run (`-n`) and asks for confirmation per controller;
- supported controllers: **PXE1610C, MP2955A, TPS53679, TPS53678**. The author
  tested only the PXE1610C on hardware. It recognises the ISL69127 but cannot
  modify it. PXE1610C NVM has a limited write counter, which the tool reads and
  reports;
- stated purpose: **210 W** 2nd-gen OEM CPUs (8259CL, 8272CL) on boards
  limited to 205 W.

**How it applies to this build:**

| question | status |
|----------|--------|
| Which VRM controller the MD71-HB0 uses | **UNKNOWN.** If it is not one of the four supported controllers, the tool cannot help. Find out first: board photos (the controller is near the CPU sockets), the forum thread, or `scan` mode on the built board (read-only). |
| Where the adapter connects on this board | **UNKNOWN.** Depends on the board's PMBus/I2C wiring; look for MD71-HB0 reports in the thread. |
| Whether 255 A is enough for the 8175M | SOURCED, anecdotal: the STH "Xeon 8259CL" thread reports the 8175M working at "240W TDP / 255A TDC" on a different board. The port itself targets 210 W parts, so the 8175M is a larger step above the board's 205 W design. |
| VRM thermals | BELIEF: a VRM designed for 205 W runs hotter at 240 W. Cool the VRMs directly and watch their temperature through the BMC under an all-core load. |

**Risks** (BELIEF, plus what the code shows): the write is permanent; writing
the wrong controller or address can disable a CPU rail and possibly the board;
the board is end of support (no BIOS fixes); warranty is void. Use the
dry run and `scan` first, and keep a record of the original ICC_MAX values the
tool prints, so they can be restored.

**Project rule:** like card flashing (`micflash`), this repository never
contains a script that writes VRM NVM. It is an operator decision and an
operator procedure. Record what was done (controller, address, old and new
values, tool version) in `results/hw/host-<date>/vrm-iccmax.txt`.

**Alternatives that avoid the modification:**
- CPUs at or below 205 W that are on the MD71-HB0's CPU support list (retail
  1st/2nd-gen Xeon Scalable; check each SKU's TDP and the list);
- a board known to accept 255 A by default (the thread reports that some
  boards already allow 255 A).

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
