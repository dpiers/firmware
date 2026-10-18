#!/bin/sh
# A normalized shared-console configuration is not an Ethernet build.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 kernel-directory" >&2; exit 1; }
sh "$(dirname "$0")/check_shared_clock_kernel.sh" "$1"
config="$1/.config"
for symbol in NT98513_ETHERNET_BOARD DWMAC_NT98513 NET INET NETDEVICES ETHERNET NET_VENDOR_STMICRO STMMAC_ETH STMMAC_PLATFORM PHYLIB PHYLINK; do
 if ! grep -qx "CONFIG_${symbol}=y" "$config"; then
  echo "NT98513 Ethernet: expected CONFIG_${symbol}=y" >&2
  exit 1
 fi
done
for symbol in DWMAC_GENERIC PM OF_DYNAMIC STMMAC_PCI DWMAC_DWC_QOS_ETH; do
 if grep -Eq "^CONFIG_${symbol}=[ym]$" "$config"; then
  echo "NT98513 Ethernet: unsupported CONFIG_${symbol}" >&2
  exit 1
 fi
done
