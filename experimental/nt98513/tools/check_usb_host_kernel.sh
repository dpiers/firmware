#!/bin/sh
# Refuse a normalized Ethernet-only image or unsupported USB lifetime.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 kernel-directory" >&2; exit 1; }
sh "$(dirname "$0")/check_ethernet_kernel.sh" "$1"
config="$1/.config"
for symbol in USB USB_FOTG210 USB_FOTG210_HCD USB_FOTG210_NT98513 NT98513_USB_HOST_BOARD; do
 if ! grep -qx "CONFIG_${symbol}=y" "$config"; then
  echo "NT98513 USB: expected CONFIG_${symbol}=y" >&2
  exit 1
 fi
done
for symbol in USB_FOTG210_UDC USB_OTG USB_ROLE_SWITCH PM OF_DYNAMIC; do
 if grep -Eq "^CONFIG_${symbol}=[ym]$" "$config"; then
  echo "NT98513 USB: unsupported CONFIG_${symbol}" >&2
  exit 1
 fi
done
