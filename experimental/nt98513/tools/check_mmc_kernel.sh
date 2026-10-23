#!/bin/sh
# Require the resolved MMC variant without weakening inherited Wi-Fi checks.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 kernel-directory" >&2; exit 1; }
sh "$(dirname "$0")/check_wifi_kernel.sh" "$1"
config="$1/.config"
for symbol in BLOCK MMC MMC_BLOCK MMC_NT98513 NT98513_MMC_BOARD; do
 if ! grep -qx "CONFIG_${symbol}=y" "$config"; then
  echo "NT98513 MMC: expected CONFIG_${symbol}=y" >&2
  exit 1
 fi
done
for symbol in MMC_SDHCI MMC_SPI MMC_TEST PM OF_DYNAMIC; do
 if grep -Eq "^CONFIG_${symbol}=[ym]$" "$config"; then
  echo "NT98513 MMC: unsupported CONFIG_${symbol}" >&2
  exit 1
 fi
done
