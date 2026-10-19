# Explicit NT98513 Wi-Fi station image

`nt98513-wifi` adds Linux 6.6.158's RTL8XXXU driver, the RTL8188FU firmware,
static wpa_supplicant 2.12 and an owner-controlled IPv4 station supervisor to
the accepted USB-host/Divinus image. This is a separate opt-in output. USB and
Ethernet DT nodes remain disabled; the accepted host's exclusive ownership,
clock/PHY/VBUS handoff and fail-stop requirements still apply. No startup
service, default network credentials or open-network fallback is installed.
See [USB-HOST.md](USB-HOST.md) for the unresolved hardware admission contract.

## Isolated offline build

Use pinned Buildroot 2024.02.10 and a new absolute output directory. Preserve
all older outputs. The source-preparation command and private Divinus cache
are the same as [ETHERNET-OWNED-INPUT.md](ETHERNET-OWNED-INPUT.md).

```
make nt98513-wifi-defconfig NT98513_BUILDROOT=/absolute/buildroot \
  NT98513_OUTPUT=/absolute/new-wifi-output \
  NT98513_DIVINUS_SOURCE_DIR=/absolute/prepared-divinus-cache
make nt98513-wifi-source NT98513_BUILDROOT=/absolute/buildroot \
  NT98513_OUTPUT=/absolute/new-wifi-output \
  NT98513_DIVINUS_SOURCE_DIR=/absolute/prepared-divinus-cache
make nt98513-wifi NT98513_BUILDROOT=/absolute/buildroot \
  NT98513_OUTPUT=/absolute/new-wifi-output \
  NT98513_DIVINUS_SOURCE_DIR=/absolute/prepared-divinus-cache
```

For an offline build, preseed Buildroot's download cache with the hash-checked
archives before invoking these targets. `source` does not grant permission to
replace pinned archives. The additional dependency closure includes libnl
3.9.0, host-pkgconf, host-bison, host-flex and host-gettext, as resolved by the
pinned Buildroot. Do not omit host prerequisites from offline preparation.

All Wi-Fi wrappers verify the Divinus archive and output identity before
calling Buildroot. Existing output variants are refused, including outputs
with malformed/duplicate configuration keys or symlink paths. Old guarded
variants and the legacy RAM wrapper refuse Wi-Fi outputs. The new kernel
fragment extends shared-clock, Ethernet and USB-host fragments without
modifying those fragments or patches 1–8. It requires built-in nl80211/mac80211,
RTL8XXXU, firmware loading, Minstrel, crypto and signed-regdb verification;
PM, OF_DYNAMIC, default wireless power save and IPv6 remain disabled.

## Pinned packages and notices

The isolated `wpa-supplicant-nt98513` package builds exactly `wpa_supplicant`
and `wpa_cli` into `/usr/sbin` with static libnl and internal crypto. It uses
an explicit WPA2-Personal configuration, with no WEP, TKIP, SAE/WPA3, WPS,
AP/P2P, EAP methods or DBus. Random-pool support remains enabled. The acquired
upstream 2.12 archive has a SHA256 pin; its detached signature was retained
but was **not authenticated**, because no trusted signer key was available.
BSD notices are installed under `/usr/share/licenses/wpa-supplicant-nt98513/`.

`rtl8188fu-firmware-nt98513` retains the full pinned
`linux-firmware-20240115.tar.xz` archive for source/legal-info. Its extraction
recipe names exactly `rtlwifi/rtl8188fufw.bin`, `LICENCE.rtlwifi_firmware.txt`
and `WHENCE`, checks regular non-symlink files and exact per-member hashes,
then installs only the FU blob and those notices. It does not substitute
an EU blob or extract the full firmware tree. The firmware remains subject
to its Realtek redistribution license; this package does not make it open
source.

The unchanged Buildroot wireless-regdb 2023.09.01 package installs both
`/lib/firmware/regulatory.db` and `regulatory.db.p7s`. The acquired signature
was verified against the exact accepted kernel's Seth Forshee certificate
with altered-data, altered-signature and wrong-key negative controls
(DAN-303 evidence `regdb-verification/receipt.json`). The final image must
still retain both exact files and the required kernel trust configuration.
This pinned database is not a current regulatory-compliance assessment.
The owner must supply the actual country; cfg80211 restrictions still apply.

The runtime package installs an inactive owner configuration example under
`/usr/share/nt98513-wifi/`, not `/etc/wpa_supplicant.conf`. `/run` already exists
in the base skeleton; the supervisor validates and privately creates its
own directories. The runtime package creates an empty mode-0644 `/etc/resolv.conf` only if
absent, preserving any existing resolver. The supervisor requires a safe
regular root-owned resolver and explicit exclusive ownership for the session. Do not run another
network manager or resolver writer concurrently.

## Image acceptance boundary

After a completed, independently checked build, package it explicitly:

```
make nt98513-wifi-image NT98513_OUTPUT=/absolute/new-wifi-output \
  NT98513_DIVINUS_SOURCE_DIR=/absolute/prepared-divinus-cache \
  NT98513_RAM_TIMESTAMP=1791936000
```

This target fixes candidate staging at `0x02c00000` and writes a new
`images/nt98513-wifi.uImage`; an existing output is never replaced. The
unchanged packager's admitted RAM end `0x03400000` enforces **header plus
payload <= 8 MiB**, along with ELF, cache-span and overlap checks. Prior
DAN-302 image growth allowance is 3,015,080 bytes. A size failure requires
reviewed compression work and independent decompressed-CPIO identity checks;
do not move staging or remove required functionality to bypass it. Passing
arithmetic does not establish physical staging ownership or a loading path.

Final artifact review must inspect static MIPS ELF/no interpreter for all
three binaries, resolved kernel and BusyBox configuration, firmware/regdb
hashes in the embedded CPIO, retained notices/legal-info and absence of
startup hooks. Source and host-test acceptance cannot establish USB
enumeration on the camera, radio calibration, association/authentication,
DHCP/DNS delivery, reassociation, throughput or soak stability. No target
execution or hardware validation is claimed here.

## Manual owner session

The USB and Ethernet ownership/clock prerequisites in `USB-HOST.md` and
`ETHERNET.md` still apply. This image does not enable either DT node or
start networking. Once those separate deployment conditions are satisfied,
the owner must provide exactly one RTL8188FU station interface and an access
point using WPA2-Personal with RSN/CCMP. The supervisor admits only the
`rtl8xxxu` driver and USB `0bda:f179`, requires the interface to have no IPv4
addresses/routes, and refuses enabled IPv6. It never configures Ethernet.

Create an absolute-path, regular, single-link root-owned mode-0600 file in a
root-owned directory with no group/world write permission. No path component
may be a symlink. Copy the inactive example, then replace its placeholders:

```
interface=wlan0
country=US
ssid=74657374
psk=<exactly 64 hexadecimal digits of the owner-derived PSK>
resolver_owner=exclusive
```

The example SSID is `test`; use the actual SSID encoded as 1–32 bytes of hex,
the actual two-letter country and the correct PSK. Derive the PSK offline on
a trusted owner machine; never put credentials in command arguments or
shell history. Unknown or repeated keys, passphrases, arbitrary supplicant
configuration, multiple networks and insecure file metadata are refused.
`resolver_owner=exclusive` asserts that no other process will write the
system resolver during this session; the supervisor also holds its own
cross-interface resolver lock. Concurrent uncooperative writers are outside
this contract and detected conflicts cause failure instead of a guessed merge.

Run the supervisor in the foreground from an owner-controlled terminal:

```
nt98513-wifi start --config /root/wifi-owner.conf
```

From another owner terminal:

```
nt98513-wifi status --interface wlan0
nt98513-wifi stop --interface wlan0
```

Status reports `ASSOCIATING`, `DHCP_WAIT`, `ONLINE` or `FAILED`. `ONLINE`
means that authentication and a validated IPv4 lease were accepted locally;
it is not proof of Internet reachability. STOP waits for the actual cleanup
result: `STOPPED` and exit zero mean completion; `FAILED`, timeout or a
nonzero exit require inspection. SIGINT/SIGTERM use the same cleanup path.
No PID file is used to signal an unrelated process. The supervisor retains
waitable ownership of its direct children until reaped, escalating from TERM
to KILL after two seconds. The lease helper is tied to its parent's lifetime.

CRNG readiness must succeed through nonblocking `getrandom` within 30 seconds;
there is no deterministic fallback. Initial association and reassociation
have a 60-second deadline. Each DHCP acquisition has an independent 30-second
deadline, with 5/10/30-second capped retry backoff. Every delivered disconnect
or changed authenticated network id/BSSID invalidates the old lease generation
before stopping DHCP. Lease expiry is enforced against monotonic time even
when association remains complete and the DHCP process stays alive; expired
or stale-generation renewals cannot revive old addresses. The admitted DHCP
policy accepts IPv4 prefixes /1 through /30, lease durations 1–604800 seconds,
an optional on-link unicast router and one to three unicast DNS servers.
No /31, /32, multicast, loopback, 0/8 or subnet network/broadcast gateway is
accepted. Lease acknowledgements wait up to 60 seconds for bounded control
and network work; an ACK is the parent's application result, not mere receipt.

Private runtime state is under `/run/nt98513-wifi/<interface>/` (0700), with
credentials and capped diagnostics (0600). Supplicant key-debug output is
never enabled. The 64-KiB diagnostic cap can terminate a noisy child; this
is reported as session failure rather than silently losing supervision.
Accepted Wi-Fi nameservers precede the saved resolver bytes; normal cleanup
restores those bytes exactly. Address and default-route removal requires the
recorded interface and complete owned tuple. Foreign replacements are left
intact and reported as conflicts. Incomplete netlink inventories are refused.

After clean STOP a fresh start may reuse the runtime directory. An unexpected
crash or cleanup conflict retains `active` and blocks restart. Do not blindly
remove that marker or kill a recorded numeric PID. The owner must inspect
private diagnostics and the actual current processes, interface addresses,
routes and resolver; resolve the conflict or restart the controlled RAM
session before retrying. The runtime deliberately has no automatic crash
recovery that guesses ownership. No hardware association, DHCP delivery,
reconnect reliability or soak stability is established by the offline tests.

## Continuous native regressions

The `shell-tests` workflow runs for PRs targeting `master` or
`codex/nt98513-*`. Its dedicated Wi-Fi job runs the unchanged 14 process and
44 network/resolver cases in a disposable Linux container, and the aggregate
Shell Tests Gate requires that job to succeed. The strict runner checks the
exact method identities and executed count; skips, missing cases, errors,
failures and expected-failure outcomes all fail the gate. The ordinary
non-root discovery job remains useful for other checks but does not supply
this native coverage.

From the firmware repository root, reproduce the dedicated job with:

```sh
docker build --platform linux/amd64 \
  -f .github/docker/nt98513-wifi-native.Dockerfile \
  -t nt98513-wifi-native:ci .github/docker
GITHUB_WORKSPACE="$PWD"
docker run --platform linux/amd64 --network none --cap-drop ALL \
  --security-opt no-new-privileges --read-only --user 0:0 \
  --pids-limit 128 --memory 512m \
  --mount "type=bind,src=$GITHUB_WORKSPACE,dst=/source,readonly" \
  --mount "type=bind,src=$GITHUB_WORKSPACE/experimental/nt98513/tests,dst=/test,readonly" \
  --tmpfs /root:rw,nosuid,nodev,noexec,mode=0700,size=32m \
  --tmpfs /run:rw,nosuid,nodev,noexec,mode=0755,size=16m \
  --tmpfs /tmp:rw,nosuid,nodev,exec,mode=1777,size=128m \
  --tmpfs /out:rw,nosuid,nodev,exec,mode=0700,size=64m \
  -e NT98513_WIFI_NATIVE_TESTS=1 -e WIFI_NET_TEST_BUILD=/out/net \
  nt98513-wifi-native:ci sh -ec '
    cc --version
    python3 --version
    dpkg-query -W build-essential gcc libc6-dev python3
    python3 /source/.github/scripts/test_nt98513_wifi_native_runner.py
    python3 /source/.github/scripts/run_nt98513_wifi_native.py
  '
```

Image acquisition/build requires public registry and Debian package access;
execution has no network access or Linux capabilities. The official Debian
base is pinned by digest; apt package resolution remains repository-dependent,
so this is a reproducible public build recipe, not a bit-identical toolchain
claim. The job prints compiler and package versions. Container UID 0 is
required to test root-owned file admission; the repository stays read-only,
and fixtures/compiler outputs live only in the private tmpfs mounts. The
command leaves its stopped container available for inspection. These tests
execute authored host-native binaries and fake child/control/network
boundaries, never the MIPS target, radio, real network configuration or vendor
firmware. Passing CI does not establish hardware behavior or replace the
separate firmware build and artifact checks.
