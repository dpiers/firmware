# NT98513 DMA start and IRQ acquisition

DAN-299 fixes a source-proven failure ordering in pinned Linux 6.6.158 with
patches1–6. Normal open called `stmmac_hw_setup()`, which started DMA, before
`stmmac_request_irq()`. If IRQ acquisition failed, `stmmac_open()` freed the
new descriptor resources without a corresponding DMA stop. The same ordering
existed in `stmmac_xdp_open()`.

Patch7 adds `dma_start_after_irq`, selected only by NT98513 glue. Normal open
passes an explicit false `start_dma` argument to hardware setup for that
policy, then starts DMA once after IRQ acquisition succeeds and before the
existing queue and DMA-interrupt enables. XDP open uses the same ordering.
The false/default policy preserves other platforms' original sequence.
There is no persistent start-state flag across failure, close or reopen.

Call-site audit against the accepted source:

* Hardware setup has two callers: normal open and resume. Resume explicitly
  passes true, preserving its existing retained-IRQ lifecycle. NT98513's
  board configuration disables PM and its glue has no PM callbacks.
* MTU reconfiguration reuses `__stmmac_open()`. Ordinary close/reopen and
  retry after a failed open therefore use the changed normal-open path.
* XDP has a separate open path, covered by this policy. The accepted image
  has `CONFIG_BPF_SYSCALL` disabled, so XDP program attachment and the
  dependent `CONFIG_XDP_SOCKETS` path are unavailable; the source change
  nevertheless preserves correct ordering if that configuration changes.
* TX error recovery and individual XSK RX/TX queue reenables operate with
  existing IRQ registration. Their start calls are unchanged.
* NT98513's capability allowlist clears FPE and EEE. Phylink link-up writes
  MAC/flow-control state but does not start DMA; NT98513 supplies no SerDes
  or speed-fix callback. These paths do not bypass the deferred start.

Run `python3 experimental/nt98513/tests/test_dma_irq_order.py`. The harness
compiles complete accepted hardware-setup, normal-open, caller cleanup,
XDP-open, resume and normal/XDP-release functions, applying the exact patch7
main.c hunks to them. Only external dependencies are stubbed. The retained
baseline fails the IRQ-error assertion with events `HDIF`: hardware setup,
DMA start, IRQ failure, descriptor free. Patched tests cover setup failure,
IRQ failure, successful start exactly once and its order, release/reopen,
retry after failure, default-policy behavior, and unchanged resume behavior.
The fixture is pinned to the accepted source; changes to those functions
require refreshing it from that source, not hand-editing a behavioral model.

This proves software ordering of newly issued DMA-start calls. It does not
prove physical reset, DMA inactivity/completion, interrupt delivery, safe
normal teardown, inherited ownership or successful traffic. Existing
stop/free behavior is unchanged, and the disabled-by-default node remains
unchanged. No camera, MMIO or firmware was executed.
