# Experimental Ethernet and Divinus composition (DAN-297)

`nt98513-ethernet-owned-input` builds the existing Ethernet kernel variant and
accepted Divinus owned-input consumer into one RAM image. It is an offline
composition target. Ethernet remains disabled in the device tree. No capture
receiver, RTOS producer/keeper, active credentials, network setup or service
startup is installed. Existing configurations and kernel patches are unchanged.

The Divinus source commit, archive, package recipe and licenses remain those in
[README.md](README.md#optional-owned-encoded-input-consumer). The immutable
package's statement that its opt-in kernel has sockets but no Ethernet describes
the original standalone `nt98513-owned-input` target. This combined target also
contains the default-disabled Ethernet driver described in
[ETHERNET.md](ETHERNET.md); the application and its requirements are unchanged.

## Reproduce

Use the pinned Buildroot 2024.02.10/Linux 6.6.158 inputs and build environment in
[README.md](README.md) and [ETHERNET.md](ETHERNET.md). Mount firmware and Divinus
source read-only. Prepare or reuse the verified source-only cache outside both
checkouts, and select a new dedicated build output:

```sh
make -C /firmware nt98513-owned-input-prepare-source \
  NT98513_DIVINUS_CHECKOUT=/divinus \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
make -C /firmware nt98513-ethernet-owned-input-source \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-ethernet-owned-input \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
# After source acquisition, build with networking disabled.
make -C /firmware nt98513-ethernet-owned-input \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-ethernet-owned-input \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
sh /firmware/experimental/nt98513/tools/check_ethernet_kernel.sh \
  /work/output-ethernet-owned-input/build/linux-custom
make -C /work/buildroot-2024.02.10 O=/work/output-ethernet-owned-input \
  BR2_EXTERNAL=/firmware/experimental/nt98513 \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source legal-info
```

For macOS Docker bind filesystems, retain the target and buildroot-fs tmpfs mounts
and `FS_DIR` forwarding documented in ETHERNET.md through final initramfs link.
The `-defconfig` target is separately available. Every combined build verifies
the archive and output variant before invoking Buildroot. Existing outputs must
have the exact package, static-library, kernel/config-fragment and BusyBox
fragment selections for that variant. Duplicate or malformed configurations,
symlinks, nonempty unknown outputs and other variants are refused. The old
owned-input target likewise refuses combined outputs. These checks do not apply
to a direct Buildroot invocation; use the wrapper targets for configuration.

The completed output can be passed to `nt98513-ram-image`, with explicit
`NT98513_RAM_TIMESTAMP`, `NT98513_RAM_STAGE` and `NT98513_RAM_IMAGE` values.
Choose and check staging against the newly built kernel and BSS; neither an
older image's size nor a candidate address establishes live memory ownership.
Retain resolved configurations, application/kernel ELF, CPIO, embedded DT,
legal-info and build logs. Inspect the static MIPS32r2 soft-float application,
manual configuration example and notices inside the final embedded CPIO.

Run host tests without executing camera code:

```sh
NT98513_TEST_DIVINUS_CHECKOUT=/divinus \
  python3 -m unittest discover -s experimental/nt98513/tests -p 'test_*.py' -v
```

## Runtime boundary

Physical MAC/PHY wiring and exclusive MAC/DMA/IRQ ownership still require the
separate Ethernet admission described in ETHERNET.md. Streaming also requires a
separately admitted `/dev/nt98513-capture` endpoint and external keeper/producer.
The consumer is launched manually with a private mode0600 configuration and
explicit `--input nt98513-owned --config /absolute/owned.conf` arguments, following
its installed APPLICATION.md. The inactive example intentionally cannot start.
This source composition does not demonstrate boot, network traffic or capture.
