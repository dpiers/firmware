# NT98513 selected-PHY software reset (DAN-298)

Patch 6 adds the stock driver's selected-PHY BMCR software reset to the
experimental Ethernet candidate. It runs once during MAC probe, after generic
Clause22 discovery and before publishing the registered MDIO bus. The default
Ethernet node remains disabled. Patches 1–5 and earlier artifacts are unchanged.

The retained `ntkimethmac.ko` (125,968 bytes, SHA-256
`7cb9387c9e7855dde4ae52a933aa5e7a9059640ebaeac68a73d3ecfd2891c81f`)
selects the first responding PHY, installs reset callback `0x10620` in bus+24
at analysis address `0x10b6c`, then registers the bus. The callback reads that
PHY's BMCR, writes the value OR `0x8000`, and polls the reset bit. Its outer
poll has no retry limit and it discards errors. These are relocated module
analysis addresses, not runtime observations.

The original raw kernel (SHA-256
`9b33b8bfaaa6b388d3c4986dc55c01901448da456aef14233ba0a71fe825f563`)
provides direct corroboration: `mdiobus_register` loads bus+24 at `0x8031089c`,
calls it through `jalr` at `0x803108a8`, and subsequently calls `mdiobus_scan`
at `0x803108d4`. This establishes invocation in the stored code, not execution
on a camera. The camera project's `evidence/nt98513-phy-reset-v1/source/`
retains the checked words, source identities and host results.

Linux 6.6.158's generic PHY driver has no `soft_reset` callback. Its first
attachment calls `phy_init_hw`, then `phy_resume`; generic resume clears
power-down but does not reset. The new hook therefore fills a specific
software behavior gap without choosing a PHY address, model or GPIO.

The NT98513 callback holds the PHY device lock. It skips any bound driver so
that a specific PHY driver retains its probe/reset/configuration sequence.
For an unbound PHY, it rejects Clause45 and `has_fixups` with `-EOPNOTSUPP`,
and an existing attachment with `-EBUSY`. An early fixup could be required by
later generic feature reads; replaying it after an arbitrary reset is not
assumed safe. The admitted current configuration has no specific PHY drivers
or target PHY fixups.

Only BMCR bit15 is added to the sampled value. Read/write/poll errors are
returned to core probe, which unregisters/frees the bus before publication.
Successful reset uses PHYLIB's polling policy: 50,000us maximum sleep per
iteration, a 600,000us requested timeout, then a 1ms settlement. This is not a
hard 600ms wall-clock deadline: the polling macro performs a final read and
underlying MDIO transactions and scheduling add latency. The deterministic
host clock reaches 650ms for a persistently set reset bit. Each MDIO operation
also retains stmmac's bounded busy polling. No error is converted to success.

Run the host checks with:

```sh
python3 -m unittest discover -s experimental/nt98513/tests -p 'test_phy_reset.py' -v
```

The harness compiles both production headers extracted from patch6. Kernel API
fixtures provide MDIO responses, locks, cleanup and time; the poll macros are
verbatim pinned Linux code with source hashes. It exercises absent callback,
missing/invalid selection, bound driver, fixups, Clause45, attached PHY, all 32
selected addresses, BMCR preservation, success, read/write/poll errors,
persistent reset, cleanup and publication ordering. A separate behavioral
baseline fixture uses the exact former completion body and demonstrates that
it publishes the bus instead of propagating the requested reset failure.

Activation ownership now explicitly includes MDIO bus and selected PHY
register access, alongside MAC/DMA/IRQ ownership. This flag cannot prove
exclusive control. Physical wiring, clocks, peer activity, PHY identity,
reset completion, link/traffic and the standard DMA lifecycle remain
unvalidated. This change adds no GPIO, power, vendor-register or fixed-PHY
assumption and does not enable the NIC or autostart networking.
