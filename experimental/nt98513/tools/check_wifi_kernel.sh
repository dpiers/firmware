#!/bin/sh
# Check the resolved configuration, never trust requested fragment symbols.
set -eu
[ "$#" -eq 1 ] || { echo "usage: $0 kernel-directory" >&2; exit 1; }
sh "$(dirname "$0")/check_usb_host_kernel.sh" "$1"
config="$1/.config"
# Reject contradictory duplicates rather than accepting the first grep match.
awk '/^CONFIG_[A-Za-z0-9_]+=/ { split($0,a,"="); if (seen[a[1]]++) exit 1 }
     /^# CONFIG_[A-Za-z0-9_]+ is not set$/ { if (seen[$2]++) exit 1 }' "$config" || {
 echo 'NT98513 Wi-Fi: duplicate kernel configuration key' >&2; exit 1;
}
for symbol in PACKET UNIX WIRELESS WLAN WLAN_VENDOR_REALTEK RTL8XXXU CFG80211 MAC80211 FW_LOADER NEW_LEDS LEDS_CLASS MAC80211_RC_MINSTREL MAC80211_RC_DEFAULT_MINSTREL CRYPTO CRYPTO_LIB_ARC4 CRC32 CRYPTO_AES CRYPTO_CCM CRYPTO_GCM CRYPTO_CMAC CRYPTO_SHA256 CRYPTO_RSA CRYPTO_SHA1 ASYMMETRIC_KEY_TYPE ASYMMETRIC_PUBLIC_KEY_SUBTYPE X509_CERTIFICATE_PARSER PKCS7_MESSAGE_PARSER SYSTEM_DATA_VERIFICATION CFG80211_REQUIRE_SIGNED_REGDB CFG80211_USE_KERNEL_REGDB_KEYS; do
 if ! grep -qx "CONFIG_${symbol}=y" "$config"; then
  echo "NT98513 Wi-Fi: expected CONFIG_${symbol}=y" >&2; exit 1
 fi
done
for symbol in PM OF_DYNAMIC CFG80211_DEFAULT_PS IPV6 RTL8XXXU_UNTESTED LEDS_GPIO; do
 if grep -Eq "^CONFIG_${symbol}=[ym]$" "$config"; then
  echo "NT98513 Wi-Fi: unsupported CONFIG_${symbol}" >&2; exit 1
 fi
done
