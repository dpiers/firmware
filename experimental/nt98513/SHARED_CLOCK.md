# Experimental shared-clock board

This third, console-based configuration integrates the accepted common clock owner and migrated watchdog. It is an **unbooted experimental image**: its enabled platform nodes would probe and perform MMIO if booted. Offline compilation and host tests do not establish arbitration, successful probe, watchdog coverage before probe, clock rates or hardware operation. No Ethernet consumer, NIC/PHY contract, capture endpoint, producer, keeper or service activation is added.

The existing RAM-console and owned-input Divinus configurations retain the base DT and original watchdog. The additive patch preserves the accepted owner/transaction/CCF/watchdog production files byte for byte; `shared-clock-source-manifest.json` records their provenance. The imported driver's historical offline warning remains unchanged. Integration-specific Kconfig, raw-request and resolved-build guards enforce the new variant's stricter policy.

Use a fresh dedicated output and the pinned Buildroot/kernel/source cache from the existing experimental instructions:

```sh
make nt98513-shared-clock-source NT98513_BUILDROOT=/path/to/buildroot-2024.02.10 NT98513_OUTPUT=/path/to/new-output
make nt98513-shared-clock NT98513_BUILDROOT=/path/to/buildroot-2024.02.10 NT98513_OUTPUT=/path/to/new-output
make nt98513-ram-image NT98513_OUTPUT=/path/to/new-output NT98513_RAM_TIMESTAMP=1736430000 NT98513_RAM_STAGE=0x02c00000
```

The image target performs structural checks; a candidate staging address is not proof of live ownership or a boot instruction. Where executable tmpfs staging is required, pass the existing `FS_DIR` child override on **every** make invocation; keep the staging parent executable and the target tmpfs separately mounted. No command above boots or flashes an image.

The board selector, owner and shared watchdog must all be built-in together, with the old watchdog off, boot-enabled handling and nowayout on, open timeout zero, and SMP/suspend/hibernation/dynamic OF off. Invalid manual requests are checked before normalization and resolved values are checked in the actual clock/DT build paths. Explicit replacement configuration targets may replace an old invalid configuration; the repository's post-configure/pre-build check still refuses a shared request that normalized to base. Maintenance targets remain usable.

The single alternate embedded DT includes the fully patched base, preserving memory, chosen console, UART and interrupt controller. It replaces only the watchdog's binding/resources and adds its dedicated owner. `openipc,clock-owner` is a zero-argument supplier phandle, not a standard `clocks` consumer reference. Two selected binding schemas do not constitute whole-board schema validation.

Host checks:

```sh
python3 experimental/nt98513/tests/test_shared_clock.py
```

Full three-variant kernel/rootfs/image regression and target validation remain separate gates. No source or test result here claims either gate has passed.
