"""Exercise output admission, normalized kernel guard and package recipes."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TREE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wifi_source_guard', TREE/'tools/prepare_divinus_source.py')
guard = importlib.util.module_from_spec(spec); spec.loader.exec_module(guard)

class WifiAdmission(unittest.TestCase):
    def test_only_intended_package_registrations_are_added(self):
        marker = '# Optional manual Wi-Fi packages; existing registrations above remain unchanged.\n'
        registrations = (TREE/'Config.in').read_text()
        self.assertEqual(registrations.count(marker), 1)
        self.assertEqual(registrations.split(marker)[1],
            'source "$BR2_EXTERNAL_NT98513_PATH/package/wpa-supplicant-nt98513/Config.in"\n'
            'source "$BR2_EXTERNAL_NT98513_PATH/package/rtl8188fu-firmware-nt98513/Config.in"\n'
            'source "$BR2_EXTERNAL_NT98513_PATH/package/nt98513-wifi-control/Config.in"\n')
        for name in ('ram','shared_clock','ethernet','owned_input','ethernet_owned_input','usb_host'):
            config = (TREE/('configs/nt98513_'+name+'_defconfig')).read_text().splitlines()
            for package in ('WPA_SUPPLICANT_NT98513','RTL8188FU_FIRMWARE_NT98513','NT98513_WIFI_CONTROL'):
                self.assertNotIn('BR2_PACKAGE_'+package+'=y',config)

    def test_signed_regdb_kernel_resolves_host_openssl_dependency(self):
        buildroot = os.environ.get('NT98513_TEST_BUILDROOT')
        if not buildroot:
            self.skipTest('NT98513_TEST_BUILDROOT supplies pinned Buildroot 2024.02.10')
        buildroot = Path(buildroot).resolve()
        self.assertTrue((buildroot/'linux/linux.mk').is_file())
        with tempfile.TemporaryDirectory() as tmp:
            probe = Path(tmp)/'probe.mk'
            # Evaluate the real upstream conditional dependency declarations.
            # Package expansion is intentionally absent: no build recipe runs.
            probe.write_text('qstrip = $(strip $(subst ",,$(1)))\n' +
                'include ' + str(TREE/'configs/nt98513_wifi_defconfig') + '\n' +
                'include linux/linux.mk\n' +
                '.PHONY: print-wifi-kernel-dependencies\n' +
                'print-wifi-kernel-dependencies:\n\t@printf "%s\\n" "$(LINUX_DEPENDENCIES)"\n')
            def dependencies(*overrides):
                result = subprocess.run(['make','--silent','-f',str(probe),
                    'print-wifi-kernel-dependencies',*overrides],cwd=buildroot,
                    capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
                return result.stdout.split()
            selected = dependencies()
            self.assertIn('host-openssl',selected)
            self.assertIn('wireless-regdb',selected)
            # Negative control: prove the dependency is supplied by this flag.
            self.assertNotIn('host-openssl',dependencies('BR2_LINUX_KERNEL_NEEDS_HOST_OPENSSL=n'))

    def test_guard_accepts_only_own_variant_and_preserves_configs(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp).resolve()/'output with spaces'; out.mkdir()
            guard.check_build_output(out, 'wifi')
            wifi = (TREE/'configs/nt98513_wifi_defconfig').read_text()
            for name in ('ram','shared_clock','ethernet','owned_input','ethernet_owned_input','usb_host','wifi'):
                config = (TREE/('configs/nt98513_'+name+'_defconfig')).read_text()
                (out/'.config').write_text(config)
                if name == 'wifi': guard.check_build_output(out, 'wifi')
                else:
                    with self.assertRaises(ValueError): guard.check_build_output(out, 'wifi')
                self.assertEqual((out/'.config').read_text(), config)
            (out/'.config').write_text(wifi)
            for old in ('owned-input','ethernet-owned-input','usb-host'):
                with self.assertRaises(ValueError): guard.check_build_output(out, old)
            (out/'.config').write_text(wifi+'\nBR2_PACKAGE_NT98513_WIFI_CONTROL=y\n')
            with self.assertRaisesRegex(ValueError, 'duplicate'): guard.check_build_output(out, 'wifi')
            (out/'.config').unlink(); (out/'.config').symlink_to(TREE/'configs/nt98513_wifi_defconfig')
            with self.assertRaises(ValueError): guard.check_build_output(out, 'wifi')

    def test_ram_wrapper_refuses_wifi_before_invoking_buildroot(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp=Path(tmp).resolve(); out=tmp/'out'; out.mkdir(); br=tmp/'br'; br.mkdir()
            (br/'Makefile').write_text('%:\n\t@touch called\n')
            (out/'.config').write_text('BR2_PACKAGE_NT98513_WIFI_CONTROL=y\n')
            result=subprocess.run(['make','--silent','-C',str(TREE.parents[1]),'nt98513-ram-defconfig',
                'NT98513_BUILDROOT='+str(br),'NT98513_OUTPUT='+str(out)],capture_output=True)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse((br/'called').exists())

    def test_kernel_guard_rejects_missing_crypto_or_unsafe_defaults(self):
        required = ('NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED '
            'NT98513_CC_BANK GPIO_NT98513 GPIOLIB OF_GPIO GPIO_CDEV NT98513_ETHERNET_BOARD '
            'DWMAC_NT98513 NET INET NETDEVICES ETHERNET NET_VENDOR_STMICRO STMMAC_ETH '
            'STMMAC_PLATFORM PHYLIB PHYLINK USB USB_FOTG210 USB_FOTG210_HCD '
            'USB_FOTG210_NT98513 NT98513_USB_HOST_BOARD PACKET UNIX WIRELESS WLAN '
            'WLAN_VENDOR_REALTEK RTL8XXXU CFG80211 MAC80211 FW_LOADER NEW_LEDS LEDS_CLASS '
            'MAC80211_RC_MINSTREL MAC80211_RC_DEFAULT_MINSTREL CRYPTO CRYPTO_LIB_ARC4 CRC32 CRYPTO_AES CRYPTO_CCM '
            'CRYPTO_GCM CRYPTO_CMAC CRYPTO_SHA256 CRYPTO_RSA CRYPTO_SHA1 ASYMMETRIC_KEY_TYPE '
            'ASYMMETRIC_PUBLIC_KEY_SUBTYPE X509_CERTIFICATE_PARSER PKCS7_MESSAGE_PARSER '
            'SYSTEM_DATA_VERIFICATION CFG80211_REQUIRE_SIGNED_REGDB CFG80211_USE_KERNEL_REGDB_KEYS').split()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp); hook=root/'arch/mips/nt98513/check-clock-config.sh'
            hook.parent.mkdir(parents=True); hook.write_text('#!/bin/sh\nexit 0\n')
            def check(values):
                (root/'.config').write_text(''.join('CONFIG_%s=%s\n'%row for row in values.items()))
                return subprocess.run(['sh',str(TREE/'tools/check_wifi_kernel.sh'),str(root)],capture_output=True)
            good=dict.fromkeys(required,'y')
            self.assertEqual(check(good).returncode,0)
            for symbol in required[25:]:
                self.assertNotEqual(check(good|{symbol:'m'}).returncode,0, symbol)
            for symbol in ('PM','OF_DYNAMIC','CFG80211_DEFAULT_PS','IPV6','RTL8XXXU_UNTESTED','LEDS_GPIO'):
                self.assertNotEqual(check(good|{symbol:'y'}).returncode,0, symbol)
            (root/'.config').write_text(''.join('CONFIG_%s=y\n'%s for s in required)+'CONFIG_RTL8XXXU=m\n')
            self.assertNotEqual(subprocess.run(['sh',str(TREE/'tools/check_wifi_kernel.sh'),str(root)],capture_output=True).returncode,0)

if __name__ == '__main__': unittest.main()
