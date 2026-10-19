"""Run real package hooks on temporary trees; no target binary execution.
Set NT98513_TEST_WIFI_INPUTS for acquired upstream input integration tests.
"""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

TREE=Path(__file__).resolve().parents[1]

class RecipeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='wifi-recipes-')
        self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve()
        self.install=shutil.which('ginstall') or shutil.which('install')

    def recipe(self, package, prefix, hook, source, **variables):
        makefile=self.root/'recipe.mk'
        makefile.write_text('include '+str(TREE/'package'/package/(package+'.mk'))+'\n'+
            '.PHONY: '+str(source/'do')+'\n'+str(source/'do')+':\n\t$('+prefix+'_'+hook+')\n')
        values={'INSTALL':self.install,'TARGET_DIR':str(self.root/'target'),
                prefix+'_PKGDIR':str(TREE/'package'/package)}|variables
        return subprocess.run(['make','--silent','-f',str(makefile),str(source/'do')]+
                              [key+'='+value for key,value in values.items()],capture_output=True,text=True)

    def test_wpa_hooks_install_only_binaries_and_notice(self):
        src=self.root/'source'; (src/'wpa_supplicant').mkdir(parents=True)
        (src/'README').write_text('upstream license notice')
        for name in ('wpa_supplicant','wpa_cli'):
            (src/'wpa_supplicant'/name).write_text('fixture binary '+name)
        p='WPA_SUPPLICANT_NT98513'; package='wpa-supplicant-nt98513'
        result=self.recipe(package,p,'CONFIGURE_CMDS',src)
        self.assertEqual(result.returncode,0,result.stderr)
        config={line.split('=',1)[0]:line.split('=',1)[1] for line in
                (src/'wpa_supplicant/.config').read_text().splitlines() if '=' in line}
        self.assertEqual(config['CONFIG_TLS'],'internal')
        self.assertEqual(config.get('CONFIG_NO_TKIP'),'y')
        self.assertTrue(all(config[k]=='y' for k in ('CONFIG_DRIVER_NL80211','CONFIG_LIBNL32','CONFIG_CTRL_IFACE','CONFIG_IEEE8021X_EAPOL')))
        for key in ('CONFIG_SAE','CONFIG_WPS','CONFIG_AP','CONFIG_P2P','CONFIG_CTRL_IFACE_DBUS_NEW','CONFIG_NO_RANDOM_POOL','CONFIG_EAP_TLS','CONFIG_DRIVER_WEXT'):
            self.assertNotIn(key,config)
        result=self.recipe(package,p,'INSTALL_TARGET_CMDS',src)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(sorted(str(f.relative_to(self.root/'target')) for f in (self.root/'target').rglob('*') if f.is_file()),
            ['usr/sbin/wpa_cli','usr/sbin/wpa_supplicant','usr/share/licenses/wpa-supplicant-nt98513/README'])

    def test_firmware_verifier_rejects_corruption_and_symlinks(self):
        inputs=os.environ.get('NT98513_TEST_WIFI_INPUTS')
        if not inputs: self.skipTest('NT98513_TEST_WIFI_INPUTS supplies the acquired source archive')
        src=self.root/'source';src.mkdir()
        result=self.recipe('rtl8188fu-firmware-nt98513','RTL8188FU_FIRMWARE_NT98513','EXTRACT_CMDS',src,
            RTL8188FU_FIRMWARE_NT98513_DL_DIR=inputs,XZCAT=shutil.which('xzcat'),TAR=os.environ.get('TAR','tar'))
        self.assertEqual(result.returncode,0,result.stderr)
        expected={'rtlwifi/rtl8188fufw.bin':'e6189d6d7032b83f0ed4284629c43db66262449a866f33fd329e611239f62e1f',
            'LICENCE.rtlwifi_firmware.txt':'a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e',
            'WHENCE':'92d477fe15d81875bc5bcc4c54df7e95b65f42e4c44d64d840f39cd0aa153d6f'}
        self.assertEqual({str(f.relative_to(src)):hashlib.sha256(f.read_bytes()).hexdigest() for f in src.rglob('*') if f.is_file()}, expected)
        result=self.recipe('rtl8188fu-firmware-nt98513','RTL8188FU_FIRMWARE_NT98513','INSTALL_TARGET_CMDS',src)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(sorted(str(f.relative_to(self.root/'target')) for f in (self.root/'target').rglob('*') if f.is_file()),
            ['lib/firmware/rtlwifi/rtl8188fufw.bin','usr/share/licenses/rtl8188fu-firmware-nt98513/LICENCE.rtlwifi_firmware.txt','usr/share/licenses/rtl8188fu-firmware-nt98513/WHENCE'])
        blob=src/'rtlwifi/rtl8188fufw.bin';original=blob.read_bytes();blob.write_bytes(b'wrong EU firmware')
        self.assertNotEqual(self.recipe('rtl8188fu-firmware-nt98513','RTL8188FU_FIRMWARE_NT98513','VERIFY_FILES',src).returncode,0)
        blob.unlink();real=self.root/'real-blob';real.write_bytes(original);blob.symlink_to(real)
        self.assertNotEqual(self.recipe('rtl8188fu-firmware-nt98513','RTL8188FU_FIRMWARE_NT98513','VERIFY_FILES',src).returncode,0)
        blob.unlink();blob.write_bytes(original)
        (src/'rtlwifi').rename(src/'real-rtlwifi');(src/'rtlwifi').symlink_to(src/'real-rtlwifi',target_is_directory=True)
        self.assertNotEqual(self.recipe('rtl8188fu-firmware-nt98513','RTL8188FU_FIRMWARE_NT98513','VERIFY_FILES',src).returncode,0)

if __name__=='__main__':unittest.main()
