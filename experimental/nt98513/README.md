# Experimental NT98513 RAM console

This opt-in target builds an independent Linux 6.6.158 BSP/watchdog and static BusyBox 1.36.1 console using Buildroot 2024.02.10's internal GCC 12.4.0, binutils 2.41 and musl 1.2.4. The ABI is little-endian MIPS32r2 O32 soft float. It bypasses the normal OpenIPC shared fragments, packages, startup scripts and flash repack. No vendor binaries, network services or automatic userspace watchdog keeper are included.

This is an owner-fork experiment, not a supported camera target. The source inputs completed a full build against firmware base `86ccd6fa1d016cacda6dfbcddc1b90fe685cba1e`; the fork base `751b2f4fb99b438fd8ec6e54c7defc662a942984` has unchanged consumed Makefile bytes, but has not received a new full build. Neither kernel boot nor hardware behavior has been tested. No flash or boot commands are provided here.

`/init` starts BusyBox init, whose rcS mounts devtmpfs/proc/sys and whose inittab offers an askfirst root shell on ttyS0. Password login is locked; the diagnostic console is deliberately available. Before userspace opens the watchdog, the kernel may feed an inherited running timer indefinitely. This does not cover firmware-to-probe expiry or establish an application health policy. Clock/IRQ/UART/RAM assumptions remain hardware-validation dependencies.

## Build in a dedicated Linux environment

Use a case-sensitive Linux filesystem and a fresh output directory. The fixed inputs are:

| Input | URL | SHA256 |
| --- | --- | --- |
| Buildroot | https://buildroot.org/downloads/buildroot-2024.02.10.tar.xz | `b193867d91ed468925a76828bd35ba64d8b4bd1ec238e35db8722fdd406926c2` |
| Linux | https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.158.tar.xz | `9eb33173b0e04e898d431587c78c47ee6e19e3fdf2ea007323de82af14bb2a4f` |

Host dependencies are a C/C++ toolchain, make, cmake, file, rsync, cpio, unzip, bc, bison, flex, patch, perl, Python 3, wget, CA certificates, git, locales, xz/bzip2/gzip/tar and pkg-config. The accepted build used Debian amd64 image ID `sha256:6a4daae0c38713dc433009d3af7f3145ae82180303701331fa230dedee3debab`. That local image ID is provenance, not a downloadable registry reference. A new compatible builder can be made with the following Dockerfile; its package versions may differ and its outputs need independent verification.

```dockerfile
FROM --platform=linux/amd64 debian@sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake file rsync cpio unzip bc bison flex patch perl python3 wget ca-certificates git locales xz-utils bzip2 gzip tar pkg-config
ENV LC_ALL=C LANG=C TZ=UTC
```

Inside that dedicated environment, mount this checkout read-only at `/firmware`, a fresh case-sensitive volume at `/work`, and the downloaded Linux archive read-only at `/inputs/linux-6.6.158.tar.xz`. Verify both archive hashes before extraction, extract Buildroot under `/work/buildroot-2024.02.10`, and provide writable executable `/tmp`. Use `SOURCE_DATE_EPOCH=1791936000`; container-root builds also require `FORCE_UNSAFE_CONFIGURE=1`. The defconfig uses `/work/dl` and supported `BR2_TAR_OPTIONS=--no-same-owner`.

```sh
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 NT98513_OUTPUT=/work/output nt98513-ram-source
# Re-enter the same retained environment with network disabled:
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 NT98513_OUTPUT=/work/output nt98513-ram
make -C /work/buildroot-2024.02.10 O=/work/output BR2_EXTERNAL=/firmware/experimental/nt98513 legal-info
```

Keep the volume, downloaded sources, build logs, resolved `.config` files, legal-info and toolchain. Do not use the default `make`/flash-repack target for this experiment. Buildroot legal-info omits custom Linux license files and its own source archive: retain Linux `COPYING`/`LICENSES`, both original archives and these patches separately.

The outputs are `/work/output/images/vmlinux.bin`, `rootfs.cpio`, and `/work/output/build/linux-custom/vmlinux`. These are raw kernel/ELF/archive files, not a stock updater or bootloader container. The source-controlled post-build script retains only Buildroot's exact unused static-musl loader placeholder outside the rootfs; unexpected paths/types/targets fail.

## Checks and retained build facts

```sh
python3 experimental/nt98513/tests/test_normalize_rootfs.py
sh -n experimental/nt98513/board/rootfs/init
sh -n experimental/nt98513/board/overlay/etc/init.d/rcS
python3 .github/scripts/ci-matrix.py --self-test
```

The local test covers seven real-filesystem normalization cases without executing target code. The existing CI selector skips full builds for draft PRs; marking this draft ready selects the normal 99-board matrix because the Makefile changes. That matrix does not build this experimental target. A new full experimental build and its artifact checks remain a separate acceptance step.

The accepted earlier build passed soft-float ABI metadata checks for BusyBox, 1,406 libc members, 1,874 libgcc members and five CRT objects. Independent checks matched raw LOAD bytes, embedded CPIO and accepted DT; 20 bounded QEMU-MIPSel 24Kc userspace invocations passed without running init, mounts or device operations. Its final ELF/raw/CPIO SHA256 values are respectively `dae328ffdc49d9464ed2a4c771f42dec21657c3d68831bdb4790f0785b3c6e59`, `8e7e665e80c7671d15dfbb102a36f64ba0cb3e0bd1e94a64c80aefd84219f9c8`, and `670da13685578094d8264c62cc117e7c524ae9b10c7441d62a9fc0b2d35cbc27`. No exhaustive instruction scan is claimed. Binaries and private build receipts are intentionally outside this source-only payload.

That image loads at physical `0x00101000`, enters at `0x0027dff0` (ELF `0x8027dff0`), and occupies 3,289,184 bytes including BSS and its 470,528-byte embedded CPIO. Unpacked regular-file pages add 507,904 bytes. Allocator metadata, process memory and transient unpacking are additional; admitted RAM `[0x00101000,0x03400000)` is not a runtime free-memory measurement. Recalculate all addresses and sizes for a new build.
