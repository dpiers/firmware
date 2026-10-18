# Experimental inherited USB host (DAN-302)

`nt98513-usb-host` combines an NT98513 USB host candidate, the existing Ethernet
variant and the accepted Divinus owned-input consumer. Both USB and Ethernet
nodes remain disabled. This is an offline build milestone: no boot, electrical
qualification, enumeration, USB transfer, Wi-Fi or camera result is claimed.
No RTL8188FU driver or firmware is included. No service starts automatically.

**An enabled candidate can panic the kernel.** After publishing Linux DMA
addresses, it must establish a bounded hardware halt before fatal notification
or buffer reclamation. If that halt fails, it panics, because retaining only the
driver's schedules would not preserve USB-core/client URB buffers. There is no
automatic reset/restart recovery. The candidate is builtin and permanent, with
no user unbind, module unload, dynamic OF or PM support.

## Build and verify offline

Use the pinned Buildroot 2024.02.10/Linux 6.6.158 environment and mounts in
[ETHERNET-OWNED-INPUT.md](ETHERNET-OWNED-INPUT.md). Prepare the same verified
Divinus source cache, then select a new dedicated output:

```sh
make -C /firmware nt98513-owned-input-prepare-source \
  NT98513_DIVINUS_CHECKOUT=/divinus \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
make -C /firmware nt98513-usb-host-source \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-usb-host \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
# After acquiring sources, build with networking disabled.
make -C /firmware nt98513-usb-host \
  NT98513_BUILDROOT=/work/buildroot-2024.02.10 \
  NT98513_OUTPUT=/work/output-usb-host \
  NT98513_DIVINUS_SOURCE_DIR=/work/divinus-source
sh /firmware/experimental/nt98513/tools/check_usb_host_kernel.sh \
  /work/output-usb-host/build/linux-custom
NT98513_TEST_DIVINUS_CHECKOUT=/divinus \
  python3 -m unittest discover -s experimental/nt98513/tests -p 'test_*.py' -v
```

The `nt98513-usb-host-defconfig` target is also available. Wrapper targets verify
the source archive and exact variant before invoking Buildroot. They reject
other variants, duplicate selections, symlinks and nonempty unknown outputs.
Existing target wrappers refuse this new output. Direct Buildroot invocations
bypass wrapper isolation. Preserve the filesystem mounts and `FS_DIR` forwarding
from ETHERNET.md through final initramfs linkage on macOS bind filesystems.

Run the existing Buildroot `legal-info` procedure against this dedicated output.
Retain resolved configurations, application/kernel ELF, final embedded CPIO/DT,
legal information and logs. Inspect both disabled DT nodes, the static MIPS32r2
soft-float Divinus ELF and its inactive configuration example. Patches 1–7,
existing defconfigs, package identity, archive and startup behavior are unchanged.
The immutable Divinus package note about sockets without an Ethernet MAC refers
to its original standalone owned-input target; this combined image contains the
default-disabled Ethernet driver. Receiver/producer admission remains separate.

## Deployment contract, deliberately absent from the supplied DT

Enabling the node alone cannot admit this driver. A separately qualified handoff
must explicitly assert `openipc,linux-exclusive-ownership`,
`openipc,stock-initialized-host-handoff` and `openipc,stable-usb-clock`, with
`dr_mode = "host"`. These declarations record obligations; they cannot establish
physical quiescence. Qualification must establish prior stock PHY, pad, role,
clock and VBUS initialization, retirement of RTOS/loader USB writers and the
stock callback, stable USB/APB clock selection throughout Linux ownership, and
correct interrupt routing. Cold initialization is outside this candidate.

Admission precedes MMIO and USB-core PHY helpers. The two DT mappings must be
controller `0xc0600000+0x400` and PHY `0xc0601000+0x200`. Hardware IRQ specifier 27
must belong to the NT98513 interrupt controller; Linux uses the positive mapped
IRQ returned by its domain. An intentional four-byte shared read-only view of
`0xc0020010` samples the current APB mux without reserving or writing clock
provider registers. Extra resources, child nodes and unsupported properties are
rejected, including external PHY/role-switch plumbing.

The runtime whitelist requires CAPLENGTH 0x10, EHCI version 1.00, one root port,
no companions, 32-bit DMA and host role. It is a conservative software boundary,
not a measured capability report. Setup snapshots inherited PHY trim masks,
executes bounded halt/reset, restores those masks and the selected Linux
wrapper/PHY sequence, and derives B304's selector from a fresh APB mux read.
Readbacks, all-ones failures, role and retained dependencies are checked before
RUN and during runtime IRQ/timer/submission paths. There are no shared-clock
writes or NT accesses to Faraday-only GMIR/OTGISR registers.

Only control/bulk URBs are accepted; interrupt/isochronous transfers, test mode
and suspend features are rejected. Root-hub polling remains USB-core emulation.
PORTSC uses W1C mask 0x2a, conditional PPC power switching and the selected stock
ignore-overcurrent policy. Physical power control, TT behavior and descriptor/
DMA compatibility require later hardware qualification.

Initialization has one allocator owner. IRQ-request, start and registration
failure paths release schedules once after safe quiescence. Core death work is
not queued until `usb_add_hcd` succeeds; an earlier fatal event either unwinds
without that work or reports death on the successfully registered permanent HCD.
Stop/shutdown close the notification gate and drain outstanding IRQ/timer/work
before reclamation. Host tests execute production policy and lifecycle helpers
with inert MMIO and modeled USB-core fault stages; they do not execute a camera
kernel or prove physical DMA cessation.
