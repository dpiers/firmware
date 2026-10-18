# Experimental NT98513 Ethernet offline milestone (DAN-296)

## Scope and design

Add one conditional Linux 6.6.158 DWMAC4 MAC/Clause-22 MDIO/PHY candidate to the experimental firmware fork. Reuse the current clock/GPIO/watchdog board, upstream stmmac DMA/NAPI and generic PHY discovery. The shipped Ethernet node stays disabled and does not invent a PHY mode, PHY ID/address, reset GPIO or MAC address. This is an offline source/build result, not a boot, link or traffic result.

Before MAC I/O, probe requires explicit Linux-exclusive ownership, a supplied MII/RMII board mode, exact named resources and a checked existing Ethernet activation clock. Preserve the observed separate four-byte C02B3008 boolean interrupt precheck without claiming its physical register role. Verify inherited TOP C0011000 bit18 and DGPIO C00100D0 bits0/1 are clear; do not write these shared settings. A snapshot does not prevent later RTOS writers. The dump contains an RTOS IRQ34/MAC-status handler, so exclusive ownership is a real hardware activation prerequisite.

Use a single queue/IRQ, 32-bit DMA and MTU1500. Discover/validate the MAC revision and PHY at runtime. Preserve stock Clause22 CSR divider encoding1. Keep the existing activation-only CCF rate0; skip DWMAC4 one-microsecond tick programming for this explicitly flagged platform and disable EEE/PTP/WOL/TSO and unsupported offloads instead of inventing a CSR rate. These are authored conservative policies. Standard stmmac DMA lifecycle remains an unvalidated hardware contract.

Patch5 adds the platform driver, narrow optional stmmac IRQ/rate hooks,
binding and disabled device tree. Patches1–4 and existing variant defaults stay
unchanged. The separate Ethernet configuration and Make targets reject other
variants' outputs and verify the resolved kernel configuration. This bounded
milestone covers source, host checks, the affected build, independent review
and owner-fork draft delivery. Required physical facts remain explicit inputs.

## Recovered contract and remaining limits

The implementation is independently authored against upstream Linux 6.6.158.
No factory binary is installed. The supplied `ntkimethmac.ko` is 125,968 bytes,
SHA-256 `7cb9387c9e7855dde4ae52a933aa5e7a9059640ebaeac68a73d3ecfd2891c81f`.
Its recovered register/descriptor/MDIO behavior supports a DWMAC4 candidate,
the MAC resource `0xc02b0000+0x2010`, IRQ34, the separate `0xc02b3008`
read/nonzero precheck and explicit MDIO divider encoding1. These are software
observations; they are not a complete hardware datasheet.

The decoded RTOS is 3,965,976 bytes, SHA-256
`47bc3e2d1e1d87b3f51654bdded2c3aa68ef5635d8eaf7af7859b98517daf2ca`.
Its 19-instruction handler at `0x03b37ee0` disables IRQ34 through the RTOS
wrapper, reads `0xc02b1160` and sets an event. This proves a compiled peer
Ethernet path exists, not that it executes on this board. The complete local
analysis and byte-checked export are retained in the camera project's
`research/nt98513-ethernet-offline-milestone.md` and
`evidence/nt98513-ethernet-offline-v1/firmware/`.

Probe accepts only MAC revision bytes `0x40` and `0x41`, with advertised
10/100 and MDIO capabilities. This whitelist is a conservative implementation
choice; the dump does not reveal the live revision. Generic Clause-22 PHY
discovery supplies an address and PHY identity at runtime. There is no assumed
PHY model, fixed link, PHY reset GPIO, MAC address or MII/RMII wiring.
The stock nominal 50MHz declaration does not establish the physical CSR clock;
the existing CCF activation clock continues to report rate0.

The disabled node in `nt98513-ethernet.dts` omits `phy-mode` and
`openipc,linux-exclusive-ownership`. Deliberate activation needs both an
independently established `mii` or `rmii` mode and a boot contract excluding
RTOS/loader MAC, DMA and IRQ access for the entire run. A DT flag and sampled
TOP/pad bits cannot prove that contract. The driver checks the existing bank
and clock provider, enables the clock, verifies inherited bits, then reads the
MAC revision before entering generic stmmac. It never programs shared TOP or
pad settings. Clock activation is sticky under the existing provider contract.

Only one queue and IRQ, 32-bit DMA, MTU1500 and speeds up to 100Mb/s are admitted.
The unknown-rate policy suppresses the one-microsecond ticker and optional
hardware offloads, Clause45 access, RIWT, EEE, PTP and WOL. The driver is builtin
and permanent, with PM, dynamic OF changes and user unbind disabled. Network
down/up still uses standard stmmac DMA teardown. Stop completion, cache
coherency, physical clocks, the extra interrupt read and repeated down/up
remain hardware validation requirements. No boot, link, packet or timing
result is claimed.

## Reproduce the offline build

Use the pinned Linux and Buildroot archives, dependencies and Linux container
setup in [README.md](README.md). Use a new dedicated output; the output guard
refuses other variants, symlinks and unrelated nonempty directories. Mount the
checkout read-only at `/firmware`, Linux at `/inputs/linux-6.6.158.tar.xz`, and
a writable case-sensitive workspace at `/work`. For this milestone use
`SOURCE_DATE_EPOCH=1736430000`, `LC_ALL=C`, `LANG=C`, `TZ=UTC`, and
`FORCE_UNSAFE_CONFIGURE=1` when building as root.

```sh
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-ethernet nt98513-ethernet-source
# Re-enter with networking disabled after the source cache is populated.
make -C /firmware NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-ethernet nt98513-ethernet
sh /firmware/experimental/nt98513/tools/check_ethernet_kernel.sh \
  /work/output-ethernet/build/linux-custom
make -C /firmware NT98513_OUTPUT=/work/output-ethernet \
  NT98513_RAM_TIMESTAMP=1736430000 NT98513_RAM_STAGE=0x02c00000 \
  NT98513_RAM_IMAGE=/work/output-ethernet/images/nt98513-ethernet-ram.uImage \
  nt98513-ram-image
```

The staging address is an arithmetic input to the existing packager, not a
verified free region on a live camera. These commands build and package only.
The Ethernet kernel exceeds the four-megabyte window above `0x03000000`, so
this variant uses `0x02c00000` for the packager's checked staging candidate.
On Docker Desktop's macOS bind filesystem, retain the established workaround:
mount `output-ethernet/target` on a 64MiB tmpfs and
`output-ethernet/build/buildroot-fs` on a 128MiB executable tmpfs, passing
`FS_DIR=/work/output-ethernet/build/buildroot-fs/stage` to every Make invocation.
Keep both mounts throughout build and final relink. A clean Linux filesystem
does not need this workaround. Keep the archives, toolchain, source tree,
resolved configurations, logs, Linux licenses and Buildroot legal-info.

Run the production-policy and target-isolation host tests with:

```sh
python3 -m unittest discover -s experimental/nt98513/tests -p 'test_ethernet*.py' -v
```

The policy harness compiles the actual headers extracted from patch5. It checks
admission, exact resources, failure/read ordering, all 256 revision bytes,
interrupt filtering and unknown-rate ticker handling; it does not emulate the
whole kernel. The target tests check configuration forwarding and refusal,
including the unchanged historical prefixes of the shared Make integration.
Existing variants are covered by the broader host suite described in README.

## Retained build result

The normal Ethernet build and separate RAM-image target exited0 on 18 October
2026. Buildroot freshly extracted Linux, applied patches1–5, rebuilt the kernel
and BusyBox, generated CPIO and completed the final initramfs link. It reused
the previously installed GCC12.4/binutils2.41/musl1.2.4 toolchain and unchanged
prerequisite packages. This is an affected-firmware build, not a clean toolchain
rebuild or a byte-reproduction experiment. All 38 experimental host-test methods
and the seven rootfs normalization cases passed independently.

The new amd64 builder image ID is
`sha256:09da52d1720e007a76956b2f90d179284e91e629e8a0bd5d74719c1b25e858eb`.
It uses the pinned Debian base in README and freshly installed host packages;
the older builder image was unavailable. Package versions, exact commands,
container settings, input hashes and preserved recovery failures are recorded
in the camera project's `evidence/nt98513-ethernet-offline-v1/build/`. Build and
packaging ran with networking disabled. No target code was executed.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Kernel ELF | 5,799,160 | `7f933021aab0646947630eaddcfc104eaf39daae52820c3704b607aecc1830c2` |
| Raw kernel | 5,014,904 | `5bd544d380f03637759d8388b560df13a29d09d84835e123b715cbf67e215606` |
| Rootfs CPIO | 484,352 | `e3a18527e0bbc9ea4981c94b1fbac69b1f00bd6e6a7aaf89b5184b7018899967` |
| Ethernet DTB | 1,911 | `8c365ce620dffe2c0f3437d87adce4468e557a60838b9e80ff47ae3668bd5171` |
| RAM image | 5,014,968 | `f2dd31bbd3d4c5a98fb729fcea0f3beed23d248dd7fd1c5e635e880f1f6fc101` |

The packager admits the loaded kernel plus BSS, the stock loader's fourfold
cache-maintenance range and the candidate staging image without overlap or
escape from the declared RAM bounds. This does not establish live ownership,
physical memory behavior or a transfer/boot procedure. The source-only delivery
does not redistribute built or factory binaries.

One independent source/artifact review found no material correctness defect.
All 14 patch5 files match the applied kernel source. The linked Ethernet
functions and driver initcall, exact embedded disabled DT and CPIO, static
BusyBox with `ifconfig`/`route`, and absence of network autostart were checked.
An independent in-memory image reconstruction matches the packaged bytes and
validates the header, CRCs and address ranges. The review remains limited to
offline source and artifacts; it supplies no physical acceptance.

Ten of the eleven repository-prescribed host suites passed. The existing
network-failover suite failed six DHCP signal assertions in the amd64 container:
its live shell-stub DHCP processes had `/proc/PID/comm` equal to `sh`, while
`S40network` requires `udhcpc` before sending signals. The test, `S40network`
and `default.script` are byte-identical to base commit `1fd34a4d`; the fixture's
process-name assumption does not hold in this environment. The failing run,
one diagnostic rerun and process observations are retained under
`evidence/nt98513-ethernet-offline-v1/repo-checks/`. No source or test was changed
to suppress these failures. This is a recorded host-verification limitation.
