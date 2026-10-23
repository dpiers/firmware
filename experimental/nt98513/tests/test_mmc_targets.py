"""Public MMC wrappers and refusal-before-mutation; host fixtures only."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

TREE = Path(__file__).resolve().parents[1]
ROOT = TREE.parents[1]
OLD = ('ram', 'shared-clock', 'ethernet', 'owned-input', 'ethernet-owned-input', 'usb-host', 'wifi')


class MmcTargets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cache_tmp = tempfile.TemporaryDirectory(prefix='mmc pinned cache ')
        cls.addClassCleanup(cls.cache_tmp.cleanup)
        cls.pinned = Path(cls.cache_tmp.name).resolve() / 'cache'
        checkout = os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')
        if not checkout:
            raise AssertionError('NT98513_TEST_DIVINUS_CHECKOUT is required')
        result = subprocess.run(['python3', str(TREE/'tools/prepare_divinus_source.py'),
                                 '--checkout', checkout, '--output-dir', str(cls.pinned)],
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='mmc targets ')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve()
        self.out = self.root/'output with spaces'
        self.br = self.root/'build root'; self.br.mkdir()
        self.cache = self.root/'cache'; shutil.copytree(self.pinned, self.cache)
        self.calls = self.br/'calls'
        # Fake only Buildroot's expensive build boundary. Real public wrappers,
        # source/output guards and RAM packager execute unchanged.
        (self.br/'Makefile').write_text("%:\n\t@printf '%s\\n' '$@' '$(O)' '$(NT98513_DIVINUS_SOURCE_DIR)' >> calls\n\t@case '$@' in *defconfig) mkdir -p '$(O)'; cp '$(BR2_EXTERNAL)/configs/$@' '$(O)/.config';; esac\n")

    def make(self, target, **extra):
        values = {'NT98513_BUILDROOT': str(self.br), 'NT98513_OUTPUT': str(self.out),
                  'NT98513_DIVINUS_SOURCE_DIR': str(self.cache)} | extra
        return subprocess.run(['make', '--no-print-directory', '--silent', '-C', str(ROOT), target]
                              + [key+'='+value for key, value in values.items()],
                              capture_output=True, text=True)

    def config(self):
        source = TREE/'configs/nt98513_mmc_defconfig'
        self.assertTrue(source.is_file(), 'MMC defconfig must exist')
        return source.read_text()

    def write_config(self, content=None):
        self.out.mkdir(exist_ok=True)
        (self.out/'.config').write_text(self.config() if content is None else content)

    def assert_refused(self, target):
        before = {str(p.relative_to(self.out)): p.read_bytes()
                  for p in self.out.rglob('*') if p.is_file()}
        result = self.make(target, NT98513_RAM_TIMESTAMP='0')
        self.assertNotEqual(result.returncode, 0, target)
        self.assertFalse(self.calls.exists(), target+' invoked Buildroot')
        after = {str(p.relative_to(self.out)): p.read_bytes()
                 for p in self.out.rglob('*') if p.is_file()}
        self.assertEqual(after, before, target+' changed output')

    def test_mmc_routes_invoke_only_own_defconfig_then_requested_goal(self):
        for target, goals in [('nt98513-mmc-defconfig', ['nt98513_mmc_defconfig']),
                              ('nt98513-mmc-source', ['nt98513_mmc_defconfig', 'source']),
                              ('nt98513-mmc', ['nt98513_mmc_defconfig', 'all'])]:
            with self.subTest(target=target):
                self.calls.unlink(missing_ok=True)
                result = self.make(target)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = [value for goal in goals for value in (goal, str(self.out), str(self.cache))]
                self.assertEqual(self.calls.read_text().splitlines(), expected)

    def test_old_targets_cannot_mutate_mmc_output(self):
        self.write_config(); (self.out/'sentinel').write_bytes(b'preserve me')
        for old in OLD:
            for suffix in ('-defconfig', '-source', ''):
                with self.subTest(target=old+suffix):
                    self.assert_refused('nt98513-'+old+suffix)
        # A valid ELF/raw pair makes this prove rejection rather than missing
        # inputs: the legacy generic RAM packager would otherwise write an image.
        self.payload(1024)
        for target in ('nt98513-ram-image', 'nt98513-wifi-image'):
            with self.subTest(target=target): self.assert_refused(target)

    def test_mmc_rejects_all_prior_variants_before_buildroot_or_packaging(self):
        for old in OLD:
            self.write_config((TREE/('configs/nt98513_'+old.replace('-', '_')+'_defconfig')).read_text())
            for target in ('nt98513-mmc-defconfig', 'nt98513-mmc-source', 'nt98513-mmc', 'nt98513-mmc-image'):
                with self.subTest(old=old, target=target): self.assert_refused(target)

    def test_corrupt_source_and_malformed_identity_fail_without_mutation(self):
        self.write_config()
        archive = next(self.cache.glob('*.tar.gz')); archive.write_bytes(archive.read_bytes()+b'corrupt')
        self.assert_refused('nt98513-mmc')
        archive.write_bytes((self.pinned/archive.name).read_bytes())
        valid = self.config()
        for bad in [valid+'\nBR2_LINUX_KERNEL=y\n', valid+'\nnot-a-config-entry\n',
                    valid.replace('BR2_PACKAGE_NT98513_WIFI_CONTROL=y', '# BR2_PACKAGE_NT98513_WIFI_CONTROL is not set'),
                    valid.replace('/board/mmc.config', '/board/mmc.config.extra')]:
            with self.subTest(config=bad[-120:]):
                self.write_config(bad); self.assert_refused('nt98513-mmc')
        self.write_config(valid)
        original = (self.out/'.config').read_bytes(); (self.out/'.config').unlink()
        (self.out/'.config').symlink_to(TREE/'configs/nt98513_mmc_defconfig')
        self.assert_refused('nt98513-mmc'); self.assertEqual((self.out/'.config').read_bytes(), original)

    def payload(self, size):
        linux = self.out/'build/linux-custom'; linux.mkdir(parents=True, exist_ok=True)
        images = self.out/'images'; images.mkdir(exist_ok=True)
        raw = b'\0'*size; ident = b'\x7fELF\x01\x01\x01'+b'\0'*9; load = 0x80101000
        header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 8, 1, load+4, 52, 0, 0x70001000, 52, 32, 1, 0, 0, 0)
        segment = struct.pack('<8I', 1, 0x100, load, load, size, size+64, 5, 0x100)
        (linux/'vmlinux').write_bytes((header+segment).ljust(0x100,b'\0')+raw)
        (images/'vmlinux.bin').write_bytes(raw)

    def test_image_keeps_eight_mib_boundary_and_fixed_stage(self):
        self.write_config(); self.payload(0x800000-64)
        result = self.make('nt98513-mmc-image', NT98513_RAM_TIMESTAMP='0', NT98513_RAM_STAGE='0x03000000')
        self.assertEqual(result.returncode, 0, result.stderr)
        decoder = json.JSONDecoder(); remaining = result.stdout; objects = []
        while remaining.strip():
            remaining = remaining.lstrip(); obj, end = decoder.raw_decode(remaining); objects.append(obj); remaining = remaining[end:]
        report = next(obj for obj in objects if 'staging_physical_span' in obj)
        self.assertEqual(report['image']['bytes'], 0x800000)
        self.assertEqual(report['staging_physical_span'], [0x02c00000, 0x03400000])
        image = self.out/'images/nt98513-mmc.uImage'; saved = image.read_bytes()
        self.payload(0x800000-63)
        oversized = self.out/'images/too-large.uImage'
        result = self.make('nt98513-mmc-image', NT98513_RAM_TIMESTAMP='0', NT98513_MMC_IMAGE=str(oversized))
        self.assertNotEqual(result.returncode, 0); self.assertIn('complete staging span', result.stderr)
        self.assertFalse(oversized.exists()); self.assertEqual(image.read_bytes(), saved)
        self.assertFalse(self.calls.exists())

    def kernel_fixture(self):
        required = ('NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED '
                    'NT98513_CC_BANK GPIO_NT98513 GPIOLIB OF_GPIO GPIO_CDEV NT98513_ETHERNET_BOARD '
                    'DWMAC_NT98513 NET INET NETDEVICES ETHERNET NET_VENDOR_STMICRO STMMAC_ETH '
                    'STMMAC_PLATFORM PHYLIB PHYLINK USB USB_FOTG210 USB_FOTG210_HCD '
                    'USB_FOTG210_NT98513 NT98513_USB_HOST_BOARD PACKET UNIX WIRELESS WLAN '
                    'WLAN_VENDOR_REALTEK RTL8XXXU CFG80211 MAC80211 FW_LOADER NEW_LEDS LEDS_CLASS '
                    'MAC80211_RC_MINSTREL MAC80211_RC_DEFAULT_MINSTREL CRYPTO CRYPTO_LIB_ARC4 CRC32 '
                    'CRYPTO_AES CRYPTO_CCM CRYPTO_GCM CRYPTO_CMAC CRYPTO_SHA256 CRYPTO_RSA CRYPTO_SHA1 '
                    'ASYMMETRIC_KEY_TYPE ASYMMETRIC_PUBLIC_KEY_SUBTYPE X509_CERTIFICATE_PARSER '
                    'PKCS7_MESSAGE_PARSER SYSTEM_DATA_VERIFICATION CFG80211_REQUIRE_SIGNED_REGDB '
                    'CFG80211_USE_KERNEL_REGDB_KEYS BLOCK MMC MMC_BLOCK MMC_NT98513 NT98513_MMC_BOARD').split()
        kernel = self.root/'kernel'; hook = kernel/'arch/mips/nt98513/check-clock-config.sh'
        hook.parent.mkdir(parents=True); hook.write_text('#!/bin/sh\nexit 0\n')
        return kernel, dict.fromkeys(required, 'y')

    def test_mmc_kernel_guard_requires_own_board_and_inherited_wifi(self):
        checker = TREE/'tools/check_mmc_kernel.sh'
        self.assertTrue(checker.is_file(), 'MMC resolved-kernel guard must exist')
        kernel, good = self.kernel_fixture()
        def check(values):
            (kernel/'.config').write_text(''.join('CONFIG_%s=%s\n'%row for row in values.items()))
            return subprocess.run(['sh', str(checker), str(kernel)], capture_output=True)
        self.assertEqual(check(good).returncode, 0)
        for symbol in ('BLOCK', 'MMC', 'MMC_BLOCK', 'MMC_NT98513', 'NT98513_MMC_BOARD', 'NT98513_USB_HOST_BOARD', 'RTL8XXXU'):
            with self.subTest(symbol=symbol): self.assertNotEqual(check(good | {symbol:'m'}).returncode, 0)
        for symbol in ('MMC_SDHCI', 'MMC_SPI', 'MMC_TEST', 'PM', 'OF_DYNAMIC'):
            with self.subTest(symbol=symbol): self.assertNotEqual(check(good | {symbol:'y'}).returncode, 0)


    def test_mmc_kernel_guard_rejects_missing_or_disabled_block(self):
        kernel, good = self.kernel_fixture()
        checker = TREE/'tools/check_mmc_kernel.sh'
        for value in (None, 'n', 'm'):
            with self.subTest(block=value):
                values = good.copy()
                if value is None:
                    values.pop('BLOCK')
                else:
                    values['BLOCK'] = value
                (kernel/'.config').write_text(''.join('CONFIG_%s=%s\n'%row for row in values.items()))
                result = subprocess.run(['sh', str(checker), str(kernel)], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('expected CONFIG_BLOCK=y', result.stderr)

    def test_mmc_fragment_overrides_disabled_base_block(self):
        values = {}
        for name in ('linux.config', 'mmc.config'):
            for line in (TREE/'board'/name).read_text().splitlines():
                if line.startswith('CONFIG_') and '=' in line:
                    key, value = line.split('=', 1); values[key] = value
                elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
                    values[line.split()[1]] = 'n'
        self.assertEqual(values.get('CONFIG_BLOCK'), 'y', 'MMC_BLOCK depends on BLOCK in Linux Kconfig')
        self.assertEqual(values.get('CONFIG_MMC_BLOCK'), 'y')

    def test_real_post_configure_and_pre_build_hooks_reject_drift(self):
        kernel, good = self.kernel_fixture()
        probe = self.root/'hook.mk'
        probe.write_text('qstrip = $(strip $(subst ",,$(1)))\n'
                         'BR2_EXTERNAL_NT98513_PATH := '+str(TREE)+'\n'
                         'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES := '+str(TREE/'board/mmc.config')+'\n'
                         'include '+str(TREE/'external.mk')+'\n'
                         '.PHONY: check\ncheck:\n'
                         '\t$(foreach hook,$(HOOKS),$(call $(hook))$(sep))\n'
                         '\t@touch "'+str(kernel/'passed')+'"\n')
        for hook_list in ('LINUX_POST_CONFIGURE_HOOKS', 'LINUX_PRE_BUILD_HOOKS'):
            def invoke(values):
                (kernel/'passed').unlink(missing_ok=True)
                (kernel/'.config').write_text(''.join('CONFIG_%s=%s\n'%row for row in values.items()))
                return subprocess.run(['make', '--silent', '-f', str(probe), 'check',
                                       'HOOKS=$('+hook_list+')'], cwd=kernel, capture_output=True, text=True)
            with self.subTest(hook=hook_list):
                result = invoke(good); self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((kernel/'passed').is_file())
                for symbol in ('MMC_NT98513', 'BLOCK'):
                    result = invoke(good | {symbol: 'n'})
                    self.assertNotEqual(result.returncode, 0, hook_list+' accepted '+symbol+' drift')
                    self.assertFalse((kernel/'passed').exists())

if __name__ == '__main__':
    unittest.main()
