"""Exercise output isolation and resolved Ethernet configuration checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

EXTERNAL = Path(__file__).resolve().parents[1]
ROOT = EXTERNAL.parents[1]
MARKER = '# Optional offline Ethernet variant; existing variants above remain unchanged.\n'
REQUIRED = ('NT98513_ETHERNET_BOARD DWMAC_NT98513 NET INET NETDEVICES ETHERNET '
            'NET_VENDOR_STMICRO STMMAC_ETH STMMAC_PLATFORM PHYLIB PHYLINK').split()


class EthernetTargets(unittest.TestCase):
    def config(self, fragment='shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config'):
        return ('BR2_LINUX_KERNEL=y\n'
                'BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="$(BR2_EXTERNAL_NT98513_PATH)/board/linux.config"\n'
                'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_NT98513_PATH)/board/' + fragment + '"\n')

    def test_foreign_output_is_never_reconfigured(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp).resolve(); br = d/'br'; br.mkdir(); out = d/'out'; out.mkdir()
            (br/'Makefile').write_text('%:\n\t@touch called\n')
            for config in ['BR2_LINUX_KERNEL=y\n', self.config('shared-clock.config'), self.config('owned-input-network.config')]:
                (out/'.config').write_text(config)
                for target in ['nt98513-ethernet-defconfig', 'nt98513-ethernet-source', 'nt98513-ethernet']:
                    p = subprocess.run(['make', '-C', str(ROOT), target,
                        'NT98513_BUILDROOT='+str(br), 'NT98513_OUTPUT='+str(out)], capture_output=True)
                    self.assertNotEqual(p.returncode, 0)
                    self.assertFalse((br/'called').exists())
                    self.assertEqual((out/'.config').read_text(), config)

    def test_valid_output_and_input_boundaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp).resolve(); out = d/'out'
            def check(path, good):
                p = subprocess.run(['python3', str(EXTERNAL/'tools/check_ethernet_output.py'), str(path)], capture_output=True)
                self.assertEqual(p.returncode == 0, good, p.stderr)
            check(out, True); out.mkdir(); check(out, True)
            (out/'keep').write_text('existing'); check(out, False)
            (out/'.config').write_text(self.config()); check(out, True)
            for suffix in ['BR2_PACKAGE_DIVINUS_OWNED_NT98513=y\n', 'BR2_LINUX_KERNEL=n\n']:
                (out/'.config').write_text(self.config()+suffix); check(out, False)
            (out/'.config').write_text(self.config())
            check('relative', False); check(str(d)+'/x/../out', False)
            alias=d/'alias'; alias.symlink_to(out, target_is_directory=True); check(alias, False)

    def test_kernel_normalization_cannot_drop_ethernet(self):
        with tempfile.TemporaryDirectory() as tmp:
            d=Path(tmp); (d/'arch/mips/nt98513').mkdir(parents=True)
            (d/'arch/mips/nt98513/check-clock-config.sh').write_text('#!/bin/sh\nexit 0\n')
            shared='NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK NT98513_WATCHDOG_SHARED NT98513_CC_BANK GPIO_NT98513 GPIOLIB OF_GPIO GPIO_CDEV'.split()
            good=dict.fromkeys(REQUIRED+shared, 'y')
            def check(values, want):
                (d/'.config').write_text(''.join('CONFIG_%s=%s\n' % v for v in values.items()))
                p=subprocess.run(['sh', str(EXTERNAL/'tools/check_ethernet_kernel.sh'), str(d)], capture_output=True)
                self.assertEqual(p.returncode==0, want, p.stderr)
            check(good, True)
            for symbol in REQUIRED: check(good|{symbol:'n'}, False)
            for symbol in ['DWMAC_GENERIC', 'PM', 'OF_DYNAMIC', 'STMMAC_PCI', 'DWMAC_DWC_QOS_ETH']:
                check(good|{symbol:'y'}, False)

    def test_existing_integration_prefixes_are_unchanged(self):
        import hashlib, json
        manifest=json.loads((EXTERNAL/'gpio-cc-source-manifest.json').read_text())
        for row in manifest['unchanged_external_files']:
            if row['path'] not in ('targets.mk', 'external.mk'):
                continue
            data=(EXTERNAL/row['path']).read_bytes()
            self.assertEqual(data.count(MARKER.encode()), 1)
            self.assertEqual(hashlib.sha256(data.split(MARKER.encode())[0]).hexdigest(), row['sha256'])

if __name__ == '__main__':
    unittest.main()
