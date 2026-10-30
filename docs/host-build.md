# Lab host build: dual Xeon Platinum 8175M on a Gigabyte MD71-HB0

The bare-metal host has two jobs: it runs the host benchmark baseline
(Milestone 2, step 20) and it hosts the MPSS VM with the Xeon Phi 5110P
passed through (`docs/qemu-passthrough.md`). Nothing here has been built or
tested yet.

Labels follow `docs/mpss-compatibility.md`: **SOURCED** (URL given),
**BELIEF**, **UNVERIFIED**, **UNKNOWN**. The Gigabyte manual PDF could not be
downloaded from the authoring environment.

## 1. Resolve first: 8175M on this board

Source for most of this section: the ServeTheHome thread "VRM modify ICC_MAX
to run high Current/TDP OEM processors"
(<https://forums.servethehome.com/index.php?threads/vrm-modify-icc_max-to-run-high-tdc-oem-cpu.38686/>),
**read in full**: all 63 pages, 1,249 posts, 2023-01-08 to 2026-10-07. Post
links below are `…/post-<id>` under that thread URL. These are forum reports,
mostly from the thread's author RolloZ170, and nobody in the thread posts an
MD71-HB0 test log, so they are labelled **SOURCED (forum)**, not PRIMARY.

Why the board matters at all: the platform firmware (ME/SPS in the PCH) reads
the VRM controller's ICC_MAX and compares it with the CPU's TDC. If ICC_MAX is
lower, "the CPU is helt in reset" and "no x86 code is executed"
(post-393799, post-454916). Retail 165–205 W SKUs have TDC 228 A; the OEM
parts above 205 W, the 8175M among them, have TDC 255 A (post-388512,
post-400615). So the question is the board's factory ICC_MAX, not its TDP
rating.

| item | status | detail |
|------|--------|--------|
| 8175M TDC / TDP | SOURCED (forum) | "8175M TDC=255A" (post-388512); "TDC=255A,TDP=240W" (post-472248). OEM (AWS) part, no Intel ARK page. |
| **8175M on the MD71-HB0** | **SOURCED (forum)** | RolloZ170, 2025-07-01: "MD71-HB0 supports OEM high TDC CPU out of the Box, e.g. Platinum P-8124 (SKL B1 TDC=255A,TDP=240W)" and "MD71-HB0 usualy runs dual Platinum 8175M (TDC=255A,TDP=240W)" (post-472248). Earlier: Gigabyte "MU71/MD71 boards with nearly the same VRM config.(5+1) allow ICC_MAX=255A by default" (post-366054). "if the factory ICC_MAX is 255A, you do NOT need the mod. any 'C' OEM or 8175M/8124M will work" (post-482664). |
| VRM heatsink | SOURCED (forum) | The MD71-HB1 "comes without VRM heatsink, i guess the reason for lower TDP support"; to run high-TDC CPUs on it "you need to install the VRM heatsinks" (post-472248). The HB0 is the variant with them: check they are present on a used board. |
| BIOS / microcode | SOURCED (forum) | "B1 is only supported out of the box by gigabyte" (post-447369): Gigabyte BIOSes carry the Skylake B1 microcode (CPUID 50653) the P-8124 needs. Not an 8175M requirement (on Dell the advice was "Platinum 8124(M) or 8175M will work", post-447361), but it shows the BIOS is not limited to the retail list. |
| Retail "205 W" rating | SOURCED (retailers) | "TDP 205W max" per socket (<https://www.serverdirect.nl/en/components/motherboards/intel/2-processors/md71-hb0/33725>). The thread's reading: boards rated 205 W "usualy do not block TDP, but TDC instead" (post-485294), and the MD71 TDC limit is already 255 A. |
| Board status | SOURCED | Gigabyte lists the MD71-HB0 as end of support (<https://www.gigabyte.com/Server-Motherboard/MD71-HB0-rev-1x>): no new BIOS releases. |
| MD71-HB0 VRM controller, I2C address, header | **UNKNOWN** | Not stated anywhere in the thread. Only needed if the mod ever is (§1b). |

**Conclusion (BELIEF, from the sources above):** expect the MD71-HB0 to boot
dual 8175M **without the ICC_MAX modification**. Plan the build that way; the
remaining risks are the ones in §1a. If a CPU does not start (no POST, the
board keeps resetting), see §1b before anything else.

### 1a. What the thread says actually goes wrong with 8175M builds

These are the reports that apply to this build, independent of the mod:

| risk | source | what to do |
|------|--------|------------|
| **Worn-out or faulty used CPUs.** An HP ML350 Gen10 with 2× 8175M had a missing GPU, blue screens, SSD drop-outs; cause: "one of, or both of the CPU's are faulty". "cheap used ones are worn out" | post-455702, post-455709 | Buy from a seller that accepts returns; test **each CPU alone** in socket 0 first; check both report the same stepping and microcode (`lscpu`, host check `microcode` log) (post-455712). |
| **Socket installation.** Same report: "sound real ylike bad socket install"; bent socket pins caused second-CPU errors on another ML350 | post-455702, post-452888, post-454063 | Torque the heatsink screws to the stated 1.2–1.3 N·m ("click") in the order printed on the cooler; make sure the leaf springs touch the cooler (post-455708). Inspect both sockets under light before fitting. |
| **Second CPU gets the first one's hot air.** "second gets hot air from first, cooling is your problem(water)"; "dual 240W CPU makes no sense, not enough place for cooler(except Water)"; the second CPU ran "+10~15C hotter" in a Dell T7920 | post-481427, post-394817, post-443379 | The MD71-HB0 sockets are in line (BELIEF from board photos; check). Use coolers whose airflow does not pass through the other cooler (90° orientation or a side-by-side duct), or liquid cooling. Watch both package temperatures under the all-core load (§1c). |
| **VRM cooling.** "keep the VRM's cool"; server boards without airflow need "proper cooling" in a desktop case | post-475105, post-469933, post-469944 | The VRM heatsinks expect server airflow: put a fan on them in a desktop/tower case and read VRM temperatures from the BMC under load. |
| **Real power draw.** "one 8175M takes usualy 180W(non AVX)"; a user expected about 520 W at the wall for two 8175M under AVX-512; dual 8259CL in a T7920: "Full load total system power draw ~600W" | post-451575, post-451576, post-442809 | PSU: 2× 240 W CPUs + 225 W 5110P + board, memory, fans; keep ≥ 1000 W of quality PSU (§2). |
| **Board power cap.** On another OEM board the "TDP of the whole machine is limited to 350W" after the CPUs ran; fixed by BMC power control "OScontrol" | post-451564, post-451598, post-451607 | The host check records the RAPL package limits (`cpu-power`). If they are below 240 W, look in the BIOS and BMC power settings and record what you change. |
| **Low clocks.** "supermicro's default settings follow intels rules": low all-core clocks fixed with the maximum PL1 time window; another board needed a BIOS reset | post-409766, post-409769, post-493806 | Record PL1/PL2/time window with every host run; do not tune them between runs. |
| **Microcode and mitigations.** Newer microcode "can cause" low clocks; one BIOS with the Downfall fix was called a possible "performance killer" | post-409809, post-408154 | Record the BIOS version and microcode (`host-check.sh` logs `microcode`) with every result. |

The 8175M is not soldered: "only thermal paste between cpu and IHS"
(post-437156). That is a statement about thermals, not an invitation to delid.

### 1b. If the mod is ever needed (operator only, never scripted here)

Only if a CPU stays in reset on this board, contradicting §1. First rule out
§1a (bad CPU, socket, BIOS settings): "IF you get a POST code a VRM mod is not
required. you have other issue" (post-442918).

The method, from the thread and from the source of its Python port
`stephensolis/vrm-iccmax` (<https://github.com/stephensolis/vrm-iccmax>, read in
full at commit `12e717c`):

- An **MCP2221A** USB–I2C board (Adafruit 4471; "old MCP2221 not work",
  post-444758) on the board's VRM PMBus, with RolloZ170's command-line tool
  `MCP2221a_iccmax_FF` (linked in the thread's first posts) or the Python port.
- `-scan 10 7F` lists the controllers it finds and **prints their current
  ICC_MAX** (read-only). `ICC_MAX= FF` means 255 A already, so nothing to do;
  `E4` is the 228 A retail default (post-493854, post-505597).
- The write sets ICC_MAX to 255 A and stores it in the controller's
  non-volatile memory. Supported controllers: PXE1610C, MP2955A, TPS53679,
  TPS53678; others (Renesas ISL69127, Infineon IR35215, MAX16500) need vendor
  tools and dongles.
- Each socket has its own controller and both must be written; a controller
  is often only alive with a CPU in its socket and the board powered
  (post-437527, post-455661).

Risks reported in the thread:

- **PXE1610C NVM can be written 14 times** ("only 14 times(OPT2)",
  post-455702; scan shows "remaining attempts= 0D", post-505597). Every write
  and every restore spends one; there is no undo command.
- **A broken connection mid-write can write the wrong place**: the
  controllers' page register is shared with the BMC and PCH, which also talk
  on that bus (post-455917, post-455926, post-487973). Recovery then needs vendor
  software and dongles (Infineon PowerCode with a PX-DNGL-01).
- **Dead adapters and damage from ground loops**: a laptop on a 2-pin AC
  adapter can sit ~100 V AC from the server ground and destroyed several
  dongles (post-508832 and post-498660, post-499804). Connect ground first.
- **Writing a RAM or chipset VRM** instead of a CPU VRM: scans also find
  those (post-489728, post-485701).
- **ESD and soldering**: some boards have no header and need wires soldered
  to series resistors (post-426018, post-426130).
- **A bricked board** in the thread (Dell T7820, post-472416) came from an
  interrupted BIOS update during the work, not the ICC_MAX write; Dell's ME
  region is bound to the PCH, so a dump from another board did not help.
- What the mod does not change: "the per phase OCP is left unchanged"
  (post-366054); "change ICC_MAX just allows to start" (post-410087).

**Project rule:** like card flashing (`micflash`), this repository never
contains a script that writes VRM NVM, and it does not script the scan either
(it needs extra hardware wired to the board). Both are operator procedures.
Record anything done (controller, address, old and new values, remaining
attempts, tool version) in `results/hw/host-<date>/vrm-iccmax.txt`.

### 1c. First power-on, in this order

The board comes with two Xeon Silver 4214 (retail Cascade Lake, 85 W). They are
on Gigabyte's support list, so they are the known-good CPUs: prove the board
with them before the 8175Ms go in, and keep them afterwards. The thread uses
supported CPUs the same way, to separate board problems from CPU problems and
for recovery (a full CMOS/NVRAM clear plus "one boot with known-good CPUs",
post-488036).

1. **With the 4214s:** both CPUs, all 12 DIMMs, BMC on the network. Enter the
   BIOS, record the BIOS and BMC versions, and update the BIOS now if needed,
   while the CPUs are supported ones (an interrupted BIOS update is how the
   thread's one bricked board died, post-472416). Set VT-x, VT-d and Above 4G
   Decoding (§3).
2. Still on the 4214s: boot Linux with the 5110P (and the GPU, §2a)
   installed, run `mic/host/host-check.sh` and keep that directory. This
   proves the slots, cards, memory and IOMMU before the CPU swap. These are
   not host baseline numbers: the baseline is taken on the 8175Ms only.
3. **Swap to one 8175M** in socket 0 (socket 1 empty, its DIMMs out per the
   manual). POST, check the BIOS sees it. Repeat with the other 8175M, so each
   is proven alone (§1a).
4. Both 8175Ms, all 12 DIMMs: boot, run `host-check.sh` again.
5. A sustained all-core load (for example `stress-ng --cpu 0` for 30 minutes,
   then an AVX-512 load) while watching both package temperatures, VRM
   temperatures (BMC) and wall power. The thread's stability recipe was
   memtest86 with ECC, then 24 h of stress-ng with `rasdaemon` (post-446806).

If an 8175M does not start but the 4214s do, go back to §1a (CPU condition,
socket) before considering §1b.

## 2. Parts checklist

| part | requirement | why |
|------|-------------|-----|
| Board | MD71-HB0: confirm in the manual that the BIOS has **VT-d**, **Above 4G Decoding** and SR-IOV/ARI options; identify which PCIe slots are wired to CPU0 vs CPU1 (block diagram) | passthrough needs VT-d; the 5110P's 8 GB BAR0 needs Above 4G decoding (`docs/qemu-passthrough.md`) |
| Slot for the 5110P | electrically **x16**, attached to a populated CPU, room for a full-length double-width card | a narrower link slows host↔card transfers (offload, deploy) |
| CPUs | 2× 8175M; the board's factory ICC_MAX is reported as 255 A (§1) | used parts: buy where returns are possible, test each alone (§1a) |
| Memory | DDR4 ECC RDIMM, **one DIMM in each of the 6 channels per socket (12 DIMMs)**, same size and speed; the board has 16 slots, so follow the manual's population order. 64–128 GB total | an unbalanced or partly filled layout starves the host baseline of memory bandwidth |
| Cooling (card) | directed front-to-back airflow through the 5110P's heatsink: server chassis or a shroud plus a high-static-pressure fan | the 5110P is passive, 225 W |
| Cooling (CPUs) | LGA3647 coolers for the board's narrow/square ILM type (check the manual), rated for 240 W; the second cooler must not breathe the first one's exhaust, or use liquid cooling | the thread's most repeated warning for dual 240 W builds (§1a) |
| Cooling (VRMs) | the board's VRM heatsinks present; a fan over them in a tower case | server boards expect chassis airflow (§1a) |
| Power supply | 2 × 240 W CPUs + 225 W card + rest; roughly 1000 W or more of quality PSU (**1600 W with the GPU**, §2a), with the PCIe auxiliary connectors the card needs and both CPU EPS connectors | forum reports: ~180 W per 8175M without AVX, ~520–600 W for a dual high-TDC system under full load (§1a) |
| Management | the board's BMC (remote console, power cycle) on the network | resetting a wedged card needs a power cycle; essential for `docs/kernel-port.md` work |
| Storage | an SSD for the host OS and VM images (60 GB per MPSS VM) | |
| Host OS | a current Linux with KVM, QEMU, libvirt, pciutils, numactl, dmidecode | |

### 2a. Optional NVIDIA GPU (Tesla T40)

**Which card it is: UNVERIFIED.** NVIDIA's public Tesla lineup has a T4 but no
T40. Cards sold as "Tesla T40" are reported to be 24 GB TU102 (Titan RTX
class) parts, which would put them near **260 W**; if it is in fact a T4, it is
**70 W** and slot-powered. Read the label, or `nvidia-smi -q` (the host check
logs it), and record which one it is.

| item | requirement |
|------|-------------|
| Slot | electrically x16, full length, double width. Prefer a slot on the **other CPU** from the 5110P (manual block diagram); the host check reports both cards' NUMA nodes (`card-numa`, `gpu-numa`). Do not put the two passive cards in adjacent slots. |
| Power connector | Tesla data-centre cards usually take an 8-pin **CPU/EPS** plug, not PCIe 8-pin (**UNVERIFIED** for this card: read the label at its power socket). A PCIe cable in an EPS socket can damage the card. |
| Cooling | passive, like the 5110P: forced front-to-back airflow through both cards. |
| Use | host-side only unless decided otherwise; it is not part of the KNC measurements. GPU results are a separate record family and never compared as "MIC". |

**Power budget** (BELIEF, from ratings and the forum figures in §1a):

| part | sustained | short peaks |
|------|-----------|-------------|
| 2× 8175M (240 W TDP; PL2 up to about 288 W) | 480 W | ~575 W |
| Xeon Phi 5110P | 225 W | 225 W |
| GPU, if ~260 W (T4: 70 W) | 260 W | ~260 W |
| board, BMC, 12 DIMMs, SSDs, fans | 100–150 W | ~150 W |
| **total** | **~1,065–1,115 W** | **~1,210 W** |

PSU: **1600 W**, 80 PLUS Platinum or Titanium, modular, with enough EPS
outputs for the board's CPU inputs plus the GPU, and the 8-pin + 6-pin PCIe
the 5110P needs. With a T4 instead, 1300 W is enough. At the wall the machine
can draw about 1.3 kW: on a 120 V / 15 A circuit (about 1,440 W continuous)
give it its own circuit.

## 3. BIOS settings (operator)

- Intel VT-x and VT-d: on.
- Above 4G Decoding (64-bit MMIO): on.
- SR-IOV / ARI: on if offered (harmless; some boards tie large-BAR handling to them).
- CPU power settings: leave PL1/PL2/time window at the BIOS defaults for the
  first runs; record them with every result. If RAPL shows a package limit
  below 240 W, the BIOS or BMC power settings are the place to look (§1a).
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
| `gpu`, `gpu-link`, `gpu-numa`, `gpu-iommu`, `gpu-driver`, `gpu-power` | optional NVIDIA GPU: never a FAIL; link x16 expected; power limit via `nvidia-smi` if installed |
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
