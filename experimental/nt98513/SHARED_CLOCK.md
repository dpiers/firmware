# Experimental shared CC, clock, watchdog and GPIO board

The existing third console configuration now includes the source-owned GPIO driver and one persistent core1 CC supplier. It is an **unbooted experimental image**. Enabled platform nodes perform MMIO during probe; source/host tests do not establish electrical pad behavior, grants, successful probe, remote liveness or watchdog coverage before probe. No NIC, PHY, capture endpoint, producer or automatic GPIO consumer is added.

RAM-console and owned-input Divinus configurations retain the base DT and original watchdog. Patches 1–3 are unchanged; patch 4 migrates the clock owner/core to the shared supplier and adds GPIO. `shared-clock-source-manifest.json` retains historical patch 3 identities; `gpio-cc-source-manifest.json` identifies the final composition, modified rows and unchanged CCF/watchdog/public-header imports. Do not attribute the new final owner/core to the old byte-for-byte import claim.

Use the existing `nt98513-shared-clock-source` and `nt98513-shared-clock` targets with pinned Buildroot, offline sources and a **fresh dedicated output**. The output-path checker prevents cross-variant replacement; it does not prove an existing tree applied patch4. Normal Buildroot must apply all four patches, configure and compile. Preserve any required executable tmpfs staging parent and pass its FS_DIR child override on every make invocation. `nt98513-ram-image` only packages completed ELF/raw outputs with explicit timestamp/staging address and structural checks; it does not load or flash them.

The shared board, CC supplier, GPIO, clock owner and shared watchdog are built-in together. GPIOLIB/OF_GPIO/GPIO_CDEV are required; old watchdog, CDEV_V1, GPIO_SYSFS, PINCTRL, SMP, suspend, hibernation and dynamic OF are off. Nowayout and boot-enabled handling remain on, open timeout zero. Raw requests and resolved configuration are checked; a shared request normalized to base is refused. Replacement/maintenance goals retain their established semantics.

The sole bank mapping is C0090050/12. Clock-v2 owns only C0020074/4 and C0020084/4 and uses resource 2 through its bank phandle. GPIO owns C0070000/64 KiB and the exact C0030070/C029001C pad words, using the same supplier and resource 4 for direction changes. Watchdog retains its clock-owner phandle. There are no GPIO children/hogs or automatic pin writes. The single alternate embedded DT preserves base memory, console, UART and interrupt-controller declarations.

GPIO uses a dynamic global base and 256 chip-relative offsets; 111 are valid: 0–23, 32–59, 64–78, 96–119, 128–135, 160–171. No physical pin names are inferred. IDs 18–23 retain observed whole-word pad side effects with unproved electrical meaning. Plain get/set do not acquire CC4; direction-output preserves direction→release observation→value ordering and is not glitch-free. Failure may leave earlier pad/direction effects. Void set cannot propagate failure through the GPIO ioctl return; read-only `fault_status` and later integer-returning operations expose refusal. No IRQ, bias, debounce, pinmux or hardware open-drain support is claimed.

Any uncertain grant/release permanently refuses new bank transactions, preserving the resource-specific cause. There is no cancellation, retry/reset or rebind recovery. Existing watchdog ping remains independent after clock preparation. Prior clock-success bits and cached positive CCF counts are not fresh admission or delivery certificates.

## Reproduce the shared build

Use the pinned Linux and Buildroot archives and dedicated Linux builder described in [README.md](README.md). Start with a fresh output, mount the checkout at `/firmware`, place Buildroot at `/work/buildroot-2024.02.10`, and provide the Linux archive at `/inputs/linux-6.6.158.tar.xz`. The following commands build this shared variant:

```sh
export SOURCE_DATE_EPOCH=1736430000
export FORCE_UNSAFE_CONFIGURE=1 # required when the dedicated builder runs as root
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-v1 nt98513-shared-clock-source
# Run the build in the same retained environment with networking disabled.
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-v1 nt98513-shared-clock
make -C /firmware NT98513_OUTPUT=/work/output-v1 \
  NT98513_RAM_TIMESTAMP=1736430000 NT98513_RAM_STAGE=0x02c00000 \
  nt98513-ram-image
```

For a Git worktree, its referenced common Git directory must also be readable inside the builder so Buildroot can record the external-tree version. A normal standalone checkout avoids that additional mount. If staging the target on tmpfs, use an executable `/work/output-v1/build/buildroot-fs` mount and pass `FS_DIR=/work/output-v1/build/buildroot-fs/stage` to every make invocation. The staging address above has passed structural checks only; it is not established as free on a camera.

## Offline validation

The normal shared firmware target completed with all four patches applied through Buildroot and all 25 mapped kernel files matching the published manifest. The build reused unchanged host tools and prerequisites, compiled a fresh kernel, and configured/built/installed BusyBox before generating the root filesystem. This was not a fresh rebuild of the entire toolchain. RAM-console and Divinus source/guard compatibility checks passed; their firmware variants were not rebuilt for this change.

All 33 firmware host test methods passed. Set `NT98513_TEST_DIVINUS_CHECKOUT` to a checkout containing the pinned Divinus commit from [README.md](README.md), then run:

```sh
python3 -m unittest discover -s experimental/nt98513/tests -p 'test_*.py'
```

Production-code native and sanitizer tests also passed, including 800 mixed GPIO/clock/supplier transactions. Independent artifact checks cover 14 compiled objects, MIPS32r2/O32 soft-float ABI, the shared DT, initcall levels, exact raw ELF LOAD bytes and embedded CPIO. An incremental controller stopped at its evidence-storage limit after producing the repeat artifacts. Separate checks confirmed 38 identical exports; the unfinished post-repeat source-inventory step was not counted as validated.

The normal build produced these outputs:

| Output | Bytes | SHA256 |
| --- | ---: | --- |
| `build/linux-custom/vmlinux` | 3,620,424 | `e4e6ea6acd160cac1622ceaff9ab90a97fb71a6bc9adf23cb41657e5404d3dd6` |
| `images/vmlinux.bin` | 3,152,640 | `ce687fc437ab083c3f59d221d18fc3e427168c2e7a943e5d07f60eca03bd1a13` |
| `images/rootfs.cpio` | 470,528 | `cb8923b7b00dd4a0e644bdaa33b052f6a670167b6a01d55d3b3c5987df4ebb6a` |
| `images/nt98513-ram.uImage` | 3,152,704 | `e78a8ff8bd83ecbe2a7b565953bf8ac0eb6aaea78b743ebd260d755f6aaa8ebd` |

The kernel enters at physical `0x00293b30`; its load/BSS span is `[0x00101000, 0x004440c0)`. These are offline layout measurements. Physical boot, GPIO operation, CC grants, watchdog supervision and capture remain untested.
