"""USB target routing and isolation against accepted existing variants."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TREE=Path(__file__).resolve().parents[1]
ROOT=TREE.parents[1]

class USBHostTargets(unittest.TestCase):
    def test_usb_variant_is_distinct_and_preserves_other_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp).resolve(); br=root/'build root';br.mkdir();out=root/'usb output'
            (br/'Makefile').write_text("%:\n\t@printf '%s\\n' '$@' '$(O)' '$(NT98513_DIVINUS_SOURCE_DIR)' >> calls\n\t@case '$@' in *defconfig) mkdir -p '$(O)'; cp '$(BR2_EXTERNAL)/configs/$@' '$(O)/.config';; esac\n")
            cache=root/'source cache'
            checkout=os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')
            self.assertTrue(checkout,'NT98513_TEST_DIVINUS_CHECKOUT is required')
            subprocess.run(['python3',str(TREE/'tools/prepare_divinus_source.py'),'--checkout',checkout,'--output-dir',str(cache)],check=True,capture_output=True)
            def make(target):
                return subprocess.run(['make','--no-print-directory','--silent','-C',str(ROOT),target,
                    'NT98513_BUILDROOT='+str(br),'NT98513_OUTPUT='+str(out),
                    'NT98513_DIVINUS_SOURCE_DIR='+str(cache)],capture_output=True,text=True)
            for target,tail in [('nt98513-usb-host-defconfig',[]),('nt98513-usb-host-source',['source']),('nt98513-usb-host',['all'])]:
                if (br/'calls').exists():(br/'calls').unlink()
                result=make(target);self.assertEqual(result.returncode,0,result.stderr)
                expected=[]
                for goal in ['nt98513_usb_host_defconfig']+tail:expected += [goal,str(out),str(cache)]
                self.assertEqual((br/'calls').read_text().splitlines(),expected)
            config=(out/'.config').read_text();(br/'calls').unlink()
            for target in ('nt98513-owned-input','nt98513-ethernet-owned-input','nt98513-ethernet','nt98513-shared-clock'):
                result=make(target);self.assertNotEqual(result.returncode,0)
                self.assertFalse((br/'calls').exists());self.assertEqual((out/'.config').read_text(),config)
            for old in ('ram','shared_clock','ethernet','owned_input','ethernet_owned_input'):
                config=(TREE/('configs/nt98513_'+old+'_defconfig')).read_text()
                (out/'.config').write_text(config)
                self.assertNotEqual(make('nt98513-usb-host').returncode,0)
                self.assertFalse((br/'calls').exists());self.assertEqual((out/'.config').read_text(),config)

    def test_resolved_usb_configuration_cannot_drop_contract(self):
        required = ('NT98513_SHARED_CLOCK_BOARD NT98513_SHARED_CLOCK '
                    'NT98513_WATCHDOG_SHARED NT98513_CC_BANK GPIO_NT98513 GPIOLIB '
                    'OF_GPIO GPIO_CDEV NT98513_ETHERNET_BOARD DWMAC_NT98513 NET '
                    'INET NETDEVICES ETHERNET NET_VENDOR_STMICRO STMMAC_ETH '
                    'STMMAC_PLATFORM PHYLIB PHYLINK USB USB_FOTG210 '
                    'USB_FOTG210_HCD USB_FOTG210_NT98513 NT98513_USB_HOST_BOARD').split()
        usb = required[-5:]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            guard = root / 'arch/mips/nt98513/check-clock-config.sh'
            guard.parent.mkdir(parents=True)
            guard.write_text('#!/bin/sh\nexit 0\n')
            def check(values, accepted):
                (root / '.config').write_text(''.join(
                    'CONFIG_%s=%s\n' % row for row in values.items()))
                result = subprocess.run(['sh', str(TREE / 'tools/check_usb_host_kernel.sh'),
                                         str(root)], capture_output=True)
                self.assertEqual(result.returncode == 0, accepted, result.stderr)
            good = dict.fromkeys(required, 'y')
            check(good, True)
            for symbol in usb:
                for value in ('n', 'm'):
                    check(good | {symbol: value}, False)
            for symbol in ('USB_FOTG210_UDC', 'USB_OTG', 'USB_ROLE_SWITCH', 'PM', 'OF_DYNAMIC'):
                for value in ('y', 'm'):
                    check(good | {symbol: value}, False)

if __name__=='__main__':unittest.main()
