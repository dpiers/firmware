# Optional NT98513 owned-input package

Default-off consumer of a separately admitted /dev/nt98513-capture endpoint.
No endpoint, keeper, active configuration, service, device node or autostart is
installed. The example has empty credentials and cannot start unchanged. Supply
an absolute private mode0600 configuration and the separate endpoint/keeper.
Closing the application does not stop RTOS acquisition. See docs/owned-input.md
for actual application behavior and source provenance.

The opt-in kernel supplies TCP/IP socket APIs, not an Ethernet MAC/PHY driver.
Manually configure loopback with `ifconfig lo 127.0.0.1 up` before local use.
A real interface/address/route requires separately supported NIC hardware and
manual setup. No DHCP, automatic network or camera startup is provided.

Normal-checkout source commit: 5e9dc9cb703d4479803e057250d31b682949497a.
This is source/build composition, not boot, capture or hardware acceptance.
