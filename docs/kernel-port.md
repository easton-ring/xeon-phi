# Porting the KNC (x100) host driver to a modern kernel (Linux 7.0)

**Status: plan only. Nothing here has been compiled against 6.x/7.0 or run on
the card.** This is a separate track from Milestone 2. The benchmarks do not
need it: the CentOS 7.3/7.4 VM with VFIO passthrough runs MPSS on a kernel it
supports, and the lab host never loads the `mic` driver (`docs/decisions.md`
D-22).

Goal: a 5110P (KNC) card runs with Intel's MPSS userspace (`mpssd`,
`micctrl`, SCIF, ideally the offload runtime) on a current kernel, first in a
development VM, then optionally on bare metal.

Labels: **VERIFIED-SRC** = read in source code during this session (commands
below); **BELIEF** = from knowledge of kernel history, to be confirmed by
compiling; **UNVERIFIED** = open question.

## 1. What exists

### 1.1 Mainline Linux carried a KNC host driver until 5.9 (VERIFIED-SRC)

`git clone --depth 1 --branch v5.9 --filter=blob:none --sparse https://github.com/torvalds/linux`

- `drivers/misc/mic/` exists in v5.9 (released 2020-10-11) and is **absent in
  v5.10**: `drivers/misc/Kconfig` and `Makefile` have no MIC entries.
- Subdirectories: `bus` (mic/scif/vop virtual buses), `host` (x100 PCIe
  driver), `card` (card-side driver), `cosm` + `cosm_client` (boot, shutdown,
  reset), `scif` (SCIF, incl. RMA), `vop` (virtio over PCIe), `common`. About
  **18.4k lines of C**. The userspace daemon is `samples/mic/mpssd` (mpssd.c +
  a `micctrl` shell script).
- Kconfig help for `INTEL_MIC_HOST`: "Intel MIC X100 devices are currently
  supported." Depends on 64BIT, PCI, X86, `select DMA_OPS`.
  `SCIF` depends on `IOMMU_SUPPORT`.
- Boot model: COSM sysfs (`bootmode=linux`, `firmware=mic/uos.img`,
  `ramdisk=mic/$card.image`). The host driver loads them with
  `request_firmware()` (`host/mic_x100.c`).
- `Documentation/misc-devices/mic/mic_overview.rst`: "The card OS as shipped by
  Intel is a Linux kernel with modifications for the X100 devices." The card
  runs the mainline **card-side** drivers (COSM client, VOP, SCIF). A
  `grep -i 'k1om|knights|MK1OM'` over `arch/x86/Kconfig*` and
  `arch/x86/Makefile` in v5.9 finds nothing: **mainline has no build target
  for the KNC CPU**. A card kernel for this stack therefore needs Intel's
  patched kernel and k1om cross-toolchain from MPSS.

### 1.2 MPSS 3.8.6 host modules, patched by Jan Just Keijser (VERIFIED-SRC)

`git clone --depth 1 https://github.com/jjkeijser/mpss` (last commit
2022-03-17)

- `mpss3/mpss-modules/` is the KNC host module source, **already carrying the
  RHEL 8.3 patch** (applying `patches/mpss-modules-4.18.0-240.el8.x86_64.patch`
  reports "reversed / previously applied" on every hunk). About **66k lines**,
  of which RAS and diagnostics are ~19k:

  | dir | lines | role |
  |-----|------:|------|
  | `micscif/` | 17,165 | SCIF incl. RMA (remote memory access) |
  | `ras/` | 16,382 | reliability/diagnostics |
  | `host/` | 12,644 | PCIe driver, uOS download, vhost (virtio blk), vcons, PM |
  | `include/` | 9,783 | headers |
  | `trace_capture/` | 2,797 | debug |
  | `vnet/` | 2,723 | virtual ethernet (`mic0` network) |
  | `dma/` | 2,371 | card DMA engine |
  | `virtio/`, `vcons/`, `pm_scif/`, `mpssboot/`, `ramoops/` | ~2,200 | |

- Patches in `mpss3/patches/` for EL7 kernels 3.10.0-862 … -1160 (CentOS
  7.5–7.9) and EL8 kernels 4.18.0-193 and -240 (CentOS 8.2/8.3). The README
  claims support "upto CentOS 7.9 and 8.5" and states that Intel's 3.8.6
  supports up to RHEL/CentOS **7.4** (kernel 3.10.0-693).
- The newest `KERNEL_VERSION()` guard in the tree is 4.14. Distro kernels
  (EL8 = "4.18" with many backports) are API-wise newer than their numbers.
- `mpss3/mpss-daemon/` contains **source** for `mpssd`, `micctrl` and
  `libmpssconfig`. The README notes some packages call `/usr/bin/python`
  (Python 2).
- `mpss4/` is the **KNL** (x200) stack and is not relevant to a 5110P.

### 1.3 A known failure (VERIFIED-SRC, README of devcapcap/XeonPhi)

On kernel 5.6 (Ubuntu 20.04, Thunderbolt enclosure), ported MPSS 3.8.6/3.6.1
modules with a KNC 3120A **froze the system**: "bad pointer deference … from
vhost_dev_init". Expect the virtio/vhost path to be a hot spot.

## 2. Two routes

| | **A. Forward-port MPSS 3.8.6 modules (Keijser base)** | **B. Revive mainline `drivers/misc/mic` (v5.9)** |
|---|---|---|
| distance | EL8 4.18 (backported) → 7.0 | 5.9 → 7.0 |
| code size | ~66k (could drop RAS/trace for a first cut: ~45k) | ~18k |
| code style | out-of-tree, version-guarded | upstream-quality, already on `pin_user_pages`, no `ioremap_nocache` |
| card OS | **Intel's MPSS 3.8.6 uOS, unchanged** | needs a card kernel with mainline's card-side drivers. Mainline can't build for KNC (§1.1), so Intel's kernel source plus the k1om toolchain is required |
| userspace | MPSS 3.8.6 `mpssd`/`micctrl` (source available), Intel tools, COI/offload runtime (binary) designed for it | `samples/mic/mpssd` only; compatibility with Intel's libscif/COI **UNVERIFIED** |
| main risk | volume of API churn; the virtio/vhost crash seen at 5.6 | the card side becomes its own project |

**Recommendation: route A**, using route B's code as a reference for how the
same hardware is driven with modern kernel idioms (DMA, `pin_user_pages`,
dmaengine, vringh). Switch to B only if A's SCIF/RMA layer proves unportable,
and only after pricing in the card-kernel build.

## 3. Development environment and safety

Never develop on the lab host's bare-metal kernel first. A bad driver can
hang the machine (§1.3) or wedge the card.

1. **Reference first.** Finish Milestone 2 in the CentOS 7.3/7.4 VM. Keep its
   `collect-diagnostics.sh` output (dmesg, `micinfo`, `micctrl -s`, SCIF
   behaviour) as the known-good baseline for comparisons.
2. **Dev guest.** A second libvirt guest (same `knc-mpss-guest.xml`, different
   disk) running the target kernel, with the card passed through. Only one
   guest may own the card at a time. A crash kills the guest, not the host.
3. **Crash capture:** serial console (`virsh console`, logged on the host),
   `netconsole` to the host, and `kdump` in the guest. Build with
   `CONFIG_DEBUG_INFO`, `CONFIG_KASAN` (on a debug kernel), `CONFIG_PROVE_LOCKING`
   and `CONFIG_DMA_API_DEBUG`.
4. **Card reset:** after a guest crash the card may need a reset. Try, in order:
   destroy and restart the guest; `echo 1 > /sys/bus/pci/devices/<BDF>/reset`
   on the host (operator); host reboot; power cycle. Whether FLR or a bus
   reset recovers a KNC is **UNVERIFIED**. Remote power control (IPMI) saves
   physical trips.
5. **Snapshots** of the dev guest before each kernel step.

## 4. Method: step through LTS kernels

Compile and functionally test at each long-term kernel instead of jumping to
7.0, so each round of API breakage is small and bisectable:

`EL8 4.18 (start) → 5.4 → 5.10 → 5.15 → 6.1 → 6.6 → 6.12 → 7.0`

At each step:

1. Build out-of-tree against that kernel's headers with
   `make -C /lib/modules/$(uname -r)/build M=$PWD W=1`. Fix errors first,
   then warnings. Modern kernels default to `-Werror` for some configs.
2. Prefer one code path for the new API. Use `#if LINUX_VERSION_CODE` only
   where a step must keep building on the previous kernel.
3. Run the functional ladder (§6) in the dev guest. Do not move on until it
   reaches at least the same rung as the previous step.
4. Commit per step with the ladder result (`results/port/<kernel>/…`).
5. Use `git bisect` across kernel versions when a rung regresses.

## 5. API change checklist

Counts are from `mic/port/apiscan.sh <tree>`, run on the trees in §1 (grep; they include version-guarded
alternatives and comments, so they are a size indicator, not exact work
items). "Changed in" is BELIEF from kernel history; the compiler is the
authority.

| API area | MPSS3+EL8 hits | mainline 5.9 hits | what changed (BELIEF) |
|----------|---------------:|------------------:|-----------------------|
| `ioremap_nocache` | 17 | 0 | removed 5.6 → `ioremap` |
| legacy `pci_map_*` / `pci_alloc_consistent` / `pci_set_dma_mask` | 18 | 5 | PCI DMA wrappers removed ~5.18 → `dma_*` on `&pdev->dev` |
| `pci_enable_msix` / `pci_enable_msi` | 1 / – | – / 1 | → `pci_alloc_irq_vectors` + `pci_irq_vector` |
| `dma_map_ops` / custom DMA ops | – | 8 | dma-mapping internals reworked repeatedly; custom ops for the card aperture are the riskiest part |
| `get_user_pages*` / `pin_user_pages` | 6 / 7 | 1 / 3 | GUP signatures changed several times (`vmas` arg removed ~6.5); DMA should use `pin_user_pages*` + `unpin_user_pages` |
| `mmap_sem` | 8 | 0 | → `mmap_read_lock()` etc. (5.8) |
| `vm_flags` writes, `remap_pfn_range` / `vm_insert_page` | 2 / 4 | 2 / 4 | `vm_flags` read-only, use `vm_flags_set()` (6.3) |
| `class_create(owner, name)` | 4 | 1 | → `class_create(name)` (6.4); `devnode` callback takes `const struct device *` |
| procfs (`create_proc_entry` / `proc_create`, `proc_ops`) | 7 / 14 | – | `create_proc_entry` long gone; `struct proc_ops` (5.6) |
| `access_ok(type, …)` | 2 | – | type argument dropped (5.0) |
| `set_fs` / `get_fs` | 6 | – | removed on x86 (5.18) → `kernel_read`/`kernel_write`, `iov_iter` |
| timers (`setup_timer`, `init_timer`, `del_timer`) | 12 | – | `timer_setup` (4.15); `del_timer*` → `timer_delete*` (6.x) |
| time (`do_gettimeofday`, `struct timeval`, `current_kernel_time`) | 6 | – | y2038 removals → `ktime_get_real_ts64()` |
| netdev (`ndo_*`, `alloc_netdev`, ethtool) | 33 | – | `ndo_tx_timeout` gained txqueue arg (5.6); `alloc_netdev` takes name-assign type; netif_napi_add weight arg removed (6.1) |
| virtio / vringh / `find_vqs` | – / 3 | 26 / 2 | `find_vqs` signature changed (6.11: `virtqueue_info`); virtio config ops evolve; **§1.3 crash site** |
| dmaengine | – (own DMA code in `dma/`) | 30 | dmaengine internal header usage (`drivers/dma/dmaengine.h`) fragile out of tree |
| `ACCESS_ONCE` | 2 | – | removed (4.15) → `READ_ONCE`/`WRITE_ONCE` |
| PM `.suspend`/`.resume`, `pci_set_power_state` | 4 | 1 | legacy PCI PM callbacks removed → `dev_pm_ops` |
| `kthread_*`, `kmap*` | 4 / 1 | 8 / – | `kmap_atomic` → `kmap_local_page` |
| `PDE_DATA` | 3 | – | → `pde_data()` (5.17) |
| timers already converted (`timer_setup`) | 8 | – | (Keijser's EL patches did part of the timer work) |
| build system | | | objtool checks, CFI/IBT (`endbr` on indirect targets; built automatically for in-kernel-flag modules), `MODULE_LICENSE` required, `-Wmissing-prototypes` as error in newer kernels |

Not counted but expected: IOMMU-API changes (SCIF depends on IOMMU), `strlcpy`
→ `strscpy`, removal of
`pci_set_dma_seg_boundary`-style helpers, `struct file_operations` field
changes, `kallsyms`/symbol exports no longer available to out-of-tree
modules.

`mic/port/apiscan.sh` in this repository reproduces the counts.

## 6. Functional ladder

Each rung needs the previous one. `mic/port/ladder.sh` runs them in the dev
guest, stops at the first failure, and writes
`results/port/<uname -r>/<UTC timestamp>/ladder.json` plus logs. Any
`BUG:`/`Oops`/`WARNING:`/`KASAN`/`DMA-API` line in dmesg during a rung fails
that rung.

| rung | check | pass condition |
|------|-------|----------------|
| L0 | environment | card `8086:2250` visible, BAR0 assigned, running in a VM (unless overridden) |
| L1 | module load | `modprobe` of the route's modules succeeds; dmesg clean |
| L2 | card ready | `micctrl -s` shows `ready` (route A) |
| L3 | card OS boot | `mpss` service start → `online` within timeout |
| L4 | host↔card network | `ping -c3 mic0` and `ssh mic0 true` |
| L5 | SCIF | a SCIF test program, if provided (`SCIF_TEST` command); otherwise skipped with reason |
| L6 | native compute | `knc_bench` on the card (deployed by `mic/native/deploy.sh`): status ok, FFT max_rel within `docs/kernels.md` bound |
| L7 | offload | `build-mic/offload_magsq --reps 3`: exit 0, three ok records |
| L8 | teardown and soak | stop the card, unload modules, dmesg clean; repeat L1–L3 `CYCLES` times |

The benchmark suite is the regression test: the same records (same commit,
same dataset SHA-256) from the reference VM and the ported kernel must agree
on `max_rel` and should agree on timings within noise.

## 7. Userspace

- `mpssd`, `micctrl`, `libmpssconfig`: source in Keijser's repo; rebuild on
  the target distro. Python 2 tools (`miccheck`, parts of `micctrl`
  helpers) need a python2.7 install or porting; Keijser documents fixing
  `/usr/bin/python` paths on EL8.
- COI / liboffload / `micnativeloadex` and the card-side runtime libraries
  come from MPSS RPMs and the icc 17 install as **binaries** built for EL7
  glibc. They usually run on newer glibc (backward compatible), but this is
  **UNVERIFIED** for this stack and is the most likely blocker for L7.
- The card OS image and its initramfs are Intel's (`/usr/share/mpss/boot/`,
  path BELIEF) and are untouched in route A.

## 8. Effort and stop criteria

Rough, for one experienced kernel developer with the card available: compile
porting to 7.0 in route A is weeks; reaching L3 (card OS boots) could be days
or open-ended depending on the DMA/IOMMU and virtio paths; L7 depends on the
binary COI stack. These are estimates, not measurements.

Stop or re-plan if:

- the same rung fails at two consecutive LTS steps after reasonable debugging;
- SCIF RMA requires kernel-internal symbols that are no longer exported;
- the card cannot be reset without a host power cycle on every crash (the
  iteration cost becomes prohibitive without remote power control).

## 9. What the operator provides

1. Milestone 2 reference VM working (CentOS 7.3/7.4), with diagnostics saved.
2. A dev guest with the target kernel, the card passed through, serial
   console logging, kdump, and snapshots.
3. Claude Code running on the lab host or inside the dev guest (this cloud
   container cannot reach the lab), with permission to build kernels and
   modules, `modprobe`/`rmmod` in the dev guest, and restart the dev guest.
4. A card reset path (§3.4), ideally remote power control.
5. The MPSS 3.8.6 tarball, Keijser's repository, the icc 17 install, and this
   repository.

## 10. Open questions

- Does FLR / secondary bus reset recover a KNC card after a guest crash?
- Do the EL7-built COI/liboffload binaries run against modern glibc and a
  ported SCIF?
- Is mainline 5.9's SCIF userspace ABI (`include/uapi/linux/scif_ioctl.h`)
  compatible with MPSS 3.8.6's `libscif`? (This decides whether route B could
  reuse Intel userspace.)
- Which RAS/trace modules can be dropped without breaking `micctrl` or
  `micinfo`?
