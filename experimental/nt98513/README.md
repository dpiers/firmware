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

## Package a completed kernel for RAM testing

The separate `nt98513-ram-image` target wraps the completed ELF/raw pair in an uncompressed legacy Linux/MIPS kernel container. It does not run a build. Supply an explicit timestamp and candidate staging address; the example address below has passed arithmetic checks only and is not established as free on a running camera.

```sh
make --no-print-directory --silent -C /firmware \
  NT98513_OUTPUT=/work/output NT98513_RAM_TIMESTAMP=1791936000 \
  NT98513_RAM_STAGE=0x03000000 nt98513-ram-image
```

The target writes `/work/output/images/nt98513-ram.uImage` and prints a JSON identity/range report. Keep the report with the image. An existing output file or symlink is refused; set `NT98513_RAM_IMAGE` to a new pathname to retain another image. Its parent directory must already exist. `NT98513_BUILDROOT` is unnecessary for packaging, and the existing build and flash targets do not invoke this target.

The packager requires an ELF32 little-endian MIPS32r2/O32 executable with one static LOAD, exact matching raw bytes and a complete aligned entry instruction. It bounds the loaded kernel and BSS, the original bootloader's fourfold cache-maintenance span, and the whole staging image including its 64-byte header within physical RAM `[0x00101000,0x03400000)`. Physical and direct cached/uncached aliases are compared before rejecting staging overlap. The ELF entry is retained separately from the load address. Both header and payload carry CRC32s.

These checks do not prove instruction/FP-ABI coverage, DT/initramfs correctness, live memory ownership, transfer method, cache visibility, watchdog continuity or RTOS coexistence. This is a RAM-test container, not a stock firmware updater or an RTOS capture installer; no boot or flash operation is performed.

## Checks and retained build facts

```sh
python3 experimental/nt98513/tests/test_normalize_rootfs.py
python3 experimental/nt98513/tests/test_ram_image_target.py
sh -n experimental/nt98513/board/rootfs/init
sh -n experimental/nt98513/board/overlay/etc/init.d/rcS
python3 .github/scripts/ci-matrix.py --self-test
```

The local test covers seven real-filesystem normalization cases without executing target code. The existing CI selector skips full builds for draft PRs; marking this draft ready selects the normal 99-board matrix because the Makefile changes. That matrix does not build this experimental target. A new full experimental build and its artifact checks remain a separate acceptance step.

The RAM-image integration tests invoke the actual make target on tiny generated ELF/raw fixtures. They check input forwarding, spaces in output paths, explicit-input requirements, error propagation and preservation of existing output. Those fixtures test the packaging interface only; they are not bootable kernel or hardware evidence.

The accepted earlier build passed soft-float ABI metadata checks for BusyBox, 1,406 libc members, 1,874 libgcc members and five CRT objects. Independent checks matched raw LOAD bytes, embedded CPIO and accepted DT; 20 bounded QEMU-MIPSel 24Kc userspace invocations passed without running init, mounts or device operations. Its final ELF/raw/CPIO SHA256 values are respectively `dae328ffdc49d9464ed2a4c771f42dec21657c3d68831bdb4790f0785b3c6e59`, `8e7e665e80c7671d15dfbb102a36f64ba0cb3e0bd1e94a64c80aefd84219f9c8`, and `670da13685578094d8264c62cc117e7c524ae9b10c7441d62a9fc0b2d35cbc27`. No exhaustive instruction scan is claimed. Binaries and private build receipts are intentionally outside this source-only payload.

That image loads at physical `0x00101000`, enters at `0x0027dff0` (ELF `0x8027dff0`), and occupies 3,289,184 bytes including BSS and its 470,528-byte embedded CPIO. Unpacked regular-file pages add 507,904 bytes. Allocator metadata, process memory and transient unpacking are additional; admitted RAM `[0x00101000,0x03400000)` is not a runtime free-memory measurement. Recalculate all addresses and sizes for a new build.

## Optional owned encoded-input consumer

The separate `nt98513-owned-input` target adds the default-off
`BR2_PACKAGE_DIVINUS_OWNED_NT98513` package. The existing console defconfig and
`nt98513-ram*` targets retain their defaults. The owned-input defconfig enables
TCP/IP socket APIs and BusyBox `ifconfig`, interface status and `route`. It does
not add a NIC driver, automatic interface setup, DHCP or any service.

This is a manual consumer of a separately admitted `/dev/nt98513-capture`
endpoint with a separate keeper/producer. The package installs `divinus-owned`,
an inactive configuration example, application/package documentation and five
license/notice files. It installs no active configuration, credentials, device
node, endpoint, keeper or autostart. Empty example credentials intentionally
cannot start the application. Closing this application does not stop RTOS
acquisition. Kernel socket support is not Ethernet or capture hardware support.

### Prepare the source once, then build offline

The source is the owner experiment at
<https://github.com/dpiers/divinus>, commit
`5e9dc9cb703d4479803e057250d31b682949497a`. Its production source ID is
`cfc60e6dcd9fa79e13731b6f07537050209e28609b2d7b7cae7fbbb81da3c006`.
Provide a local Git checkout containing that commit, mounted read-only at
`/divinus`. Preparation reads the commit's Git blobs, not current HEAD or working
files; the later CI-only revision is not the documentation pin for this archive.
No GitHub-generated archive or compiled binary is substituted.

Use absolute paths without symlink components for the cache and build output.
The writable cache must be outside both source checkouts. The helper combines
47 hash-checked upstream files with the package's exact `PACKAGE.md`, verifies
all 48 members, and reproduces the source-only archive with SHA-256
`ed4b016b32d00b5b36eb98d5395dca28bc2a74161164918eeac45c57e8051f85`.
It refuses mismatched existing output and publishes only after every check
passes. An identical existing archive is preserved. No source download occurs
inside this helper; obtain the pinned public checkout before the offline build.

```sh
make -C /firmware nt98513-owned-input-prepare-source \
  NT98513_DIVINUS_CHECKOUT=/divinus \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
make -C /firmware nt98513-owned-input-source \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-owned-input \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
# Re-enter the retained build environment with networking disabled:
make -C /firmware nt98513-owned-input \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-owned-input \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
make -C /work/buildroot-2024.02.10 O=/work/output-owned-input \
  BR2_EXTERNAL=/firmware/experimental/nt98513 \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source legal-info
```

`nt98513-owned-input-defconfig` is also available separately. These targets
check the full archive hash before invoking Buildroot and pass the same cache
path to each recursive Make. Use a new, empty output or one already configured
for this package; console and unrelated nonempty outputs are refused. Raw
Buildroot invocations also require `NT98513_DIVINUS_SOURCE_DIR` when the package
is enabled. Buildroot's package hash check remains active. Retain legal-info's
source archive, package metadata and all five license files, in addition to the
custom Linux/Buildroot legal inputs described above.

Host-only checks require a local copy of the same pinned source:

```sh
NT98513_TEST_DIVINUS_CHECKOUT=/divinus \
  python3 -m unittest discover -s experimental/nt98513/tests -p 'test_*.py'
```

The host CI job runs these checks with pinned public source and read-only
permissions. It tests source preparation, actual Make forwarding and existing
rootfs/image guard logic. Its Buildroot recorder does not compile firmware;
it neither repeats the application's protocol test harness nor establishes a
complete integration build. The existing conservative CI selector still treats
experimental paths as unknown for a ready PR; a draft PR skips its large board
build matrix. No release workflow is changed.

The package and networking fragment enlarge the kernel and CPIO. Package a
completed owned-input output using `nt98513-ram-image` with its own output path,
explicit timestamp and checked candidate staging address. Recompute the ELF,
BSS, cache-maintenance envelope, CPIO size and unpacked resident pages; the
console image measurements above do not describe this optional image. A normal
checkout full build/legal/repeat/image validation remains required before build
acceptance. No target execution, installed endpoint, live free-RAM, capture,
Ethernet or boot acceptance is claimed by these source/host checks.


### Shared CC/GPIO source increment

The existing optional shared-clock target also has a source-owned sparse GPIO
implementation and a sole cross-core bank supplier; see [SHARED_CLOCK.md](SHARED_CLOCK.md).
Patch 4 and `gpio-cc-source-manifest.json` distinguish final migrated sources from
the historical patch 3 imports. The normal shared firmware build and all 33 host
test methods passed. [SHARED_CLOCK.md](SHARED_CLOCK.md) records the exact build
commands, artifact identities and validation limits. The base and Divinus
configurations/package remain unchanged; source and guard compatibility checks
passed without rebuilding those variants. Physical boot, GPIO and capture
validation remain separate future work. No GPIO consumer, service or activation
is installed.

### Optional Ethernet source candidate

The separate `nt98513-ethernet` target adds a conditional MAC/Clause-22 PHY
candidate to the shared board. Its Ethernet node remains disabled; activation
requires independently established board wiring and exclusive ownership.
See [ETHERNET.md](ETHERNET.md) for the firmware evidence, build commands,
offline validation and unresolved hardware contracts.

## Combined Ethernet and owned-input consumer

The separate `nt98513-ethernet-owned-input` target combines the existing disabled
Ethernet board with the pinned manual Divinus consumer. See
[ETHERNET-OWNED-INPUT.md](ETHERNET-OWNED-INPUT.md) for isolated-output reproduction
and the remaining capture and hardware prerequisites.
