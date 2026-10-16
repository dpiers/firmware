#!/bin/sh
# A normalized base configuration is not a successful shared-clock variant.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 kernel-directory" >&2; exit 1; }
config="$1/.config"
for symbol in NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED; do
 if ! grep -qx "CONFIG_${symbol}=y" "$config"; then
  echo "NT98513: expected shared variant lost CONFIG_${symbol}=y" >&2
  exit 1
 fi
done
exec sh "$1/arch/mips/nt98513/check-clock-config.sh" "$config"
