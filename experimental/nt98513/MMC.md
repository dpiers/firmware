# NT98513 native SD PIO candidate

This opt-in variant adds a native SD controller candidate to the existing Wi-Fi
image. Both MMC controller nodes, USB and Ethernet remain **disabled** in the
embedded `nt98513-mmc.dtb`. A successful source test or image build does not
qualify board ownership, socket routing, electrical behavior or storage writes.
No automount, filesystem creation, storage-write service or new startup hook is
installed. Wi-Fi and Divinus retain their existing manual startup policy.

The two admitted controller descriptions are C0420000/0x200 with hardware IRQ30
and clock index0, and C0500000/0x200 with hardware IRQ31 and index1. These do not
identify which instance reaches a physical card socket. Before enabling either,
a separate board qualification must establish exclusive controller and previous
DMA-owner handoff, the inherited card rail/pad state and stable nominal192MHz
clock source. DT assertions record those obligations; they cannot establish them.
The candidate does not initialize cold-board power or pads and does not map the
GPIO supplier's C003 resource a second time.

The host scope is native SD, 1-bit legacy timing at nominal400kHz through12MHz,
3.3V, command/response enumeration and one PIO block of1..512bytes. Small SCR and
status transfers are included. There are no DMA descriptors, SDIO, MMC/eMMC,
multi-block transfers, high-speed/UHS, tuning or voltage switching. Physical
write-protect sensing is not inferred. The selected MMC block layer can expose a
card only after separately authorized activation and successful enumeration.

The host does not claim the recovered all-four-bit B40 predicate is DAT0. It has
no `.card_busy` callback or WAIT_WHILE_BUSY capability. The pinned Linux6.6.158
native SD paths poll CMD13 after writes and CMD38 erase; ordinary CMD32/33/38
transport is supported. A large card busy timeout alone is not a host transaction
failure. Core card-busy deadlines and each host command/PIO/recovery deadline are
separate. Unsupported arbitrary raw-ioctl busy operations are outside this
reviewed contract. Single-block recovery's standalone CMD12/CMD13 must remain
available even though an attached multi-block stop is rejected.

## Source and clock ownership

Patches1–8 and previous variants remain intact. Patch9 extends the existing
shared-clock owner with an opt-in v3 resource layout and explicit sleepable MMC
operations. Patch10 supplies the host, its binding and the MMC board DT. The host
uses `include/linux/soc/nt98513_mmc_clock.h`; it never owns C002/C009 mappings or
introduces a second clock writer. Existing v2 watchdog/Ethernet behavior remains
separate from new MMC state.

Read-only clock admission precedes controller reads; controller idle checks
precede mutations. The first admitted start has a single prepare pulse. Clock0
stops the selected clock only after verified idle; the next rate request resumes
without a new pulse or initial-state admission. Owned/sibling fields and phase
policy are checked under the existing owner/CC-bank serialization. Returned rates
are readback-derived nominal values, not physical frequency measurements.

Requests use bounded worker-side PIO and a private bounce buffer. Command and
data milestones, actual FIFO byte progress, and errors are tracked separately.
IRQ handling cannot complete requests or traverse SG data. Terminal cleanup must
finish before exactly-once completion, with locks released and no request access
after the callback. Failed stop/uncertain ownership latches the host offline;
it is not permission to reset or gate an unquiesced controller.

## Isolated build and packaging

Use a new dedicated output directory and the retained pinned Buildroot2024.02.10,
Linux6.6.158 archive, GCC12.4.0/binutils2.41/musl1.2.4 toolchain and source caches.
The userspace configuration is inherited from `nt98513_wifi_defconfig`; the new
fragment adds `BLOCK`, `MMC`, `MMC_BLOCK`, `MMC_NT98513` and
`NT98513_MMC_BOARD` built-in. `BLOCK` must be enabled explicitly because the base
RAM configuration disables it and Linux makes `MMC_BLOCK` depend on it. Without
that prerequisite, Kconfig also drops the dependent host and MMC board symbols.
`NT98513_USB_HOST_BOARD` remains selected for the inherited contract; MMC has
precedence in the one-DT selection. The MMC post-configure and pre-build hooks also enforce the
inherited Wi-Fi/USB/Ethernet checks. Required symbols must survive Kconfig
normalization rather than merely appear in a fragment.

Prepare or verify the pinned Divinus source archive using the documented
`nt98513-owned-input-prepare-source` / `nt98513-owned-input-check-source` workflow.
For the MMC build, supply explicit absolute paths:

```sh
make NT98513_BUILDROOT=/path/to/buildroot-2024.02.10 \
  NT98513_OUTPUT=/path/to/new-mmc-output \
  NT98513_DIVINUS_SOURCE_DIR=/path/to/pinned-divinus-cache nt98513-mmc-source
make NT98513_BUILDROOT=/path/to/buildroot-2024.02.10 \
  NT98513_OUTPUT=/path/to/new-mmc-output \
  NT98513_DIVINUS_SOURCE_DIR=/path/to/pinned-divinus-cache nt98513-mmc
make NT98513_OUTPUT=/path/to/new-mmc-output \
  NT98513_DIVINUS_SOURCE_DIR=/path/to/pinned-divinus-cache \
  NT98513_RAM_TIMESTAMP=1736430000 nt98513-mmc-image
```

`nt98513-mmc-defconfig` is available separately. The source/archive/output guard
runs before Make can change an output, including during packaging. Prior variant
outputs are refused by the MMC wrappers; old variant build targets and the
legacy RAM image wrapper refuse MMC outputs. Use separate directories rather
than reconfiguring accepted outputs in place. Packaging only consumes completed
ELF/raw files; it does not compile or execute a target.

The default package is `images/nt98513-mmc.uImage`. Staging remains0x02c00000 and
the **complete header plus payload must fit8MiB**. The existing ELF/BSS, cache,
RAM, overlap and CRC checks remain in force. `NT98513_RAM_STAGE` cannot move the
MMC stage to evade this limit. Inspect actual final ELF, embedded CPIO and DT;
requested configuration or a synthetic boundary test is not proof of the actual
candidate's size or RAM fit. Buildroot legal-info and retained supplemental
source/license bindings remain part of artifact review.

## Offline checks and remaining gates

Host tests must compile the actual authored clock and host transaction/lifecycle
paths, including both instance masks, faults, no-write rejection, pulse order,
clock0 resume, FIFO tails, interrupt order, deadlines, immediate callback
resubmission and shutdown. Run focused discovery after supplying the pinned
Divinus checkout required by the wrapper fixture:

```sh
NT98513_TEST_DIVINUS_CHECKOUT=/path/to/pinned/divinus \
  python3 -m unittest discover -s experimental/nt98513/tests -p 'test_mmc*.py'
```

Then run the relevant existing experimental and repository checks. The wrapper
suite executes Make with a host-only fake Buildroot boundary and runs the real
source/output guards and packager on synthetic ELF data; it cannot establish
real kernel build success. The ordinary experimental discovery does not replace
the dedicated58-case native Wi-Fi runner described in `WIFI.md`.

Artifact review must establish the actual single linked disabled MMC DT,
configuration/driver identities, complete image bound, unchanged userspace and
firmware/regdb/notices, and preservation of prior outputs. Hardware gates remain:
qualified ownership/power/pads/clock, physical socket routing, enumeration,
response/FIFO/IRQ behavior, card compatibility and storage durability. Nothing in
this document authorizes flashing, mounting, formatting or writes to a card.
