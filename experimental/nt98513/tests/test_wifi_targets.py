"""Run public make wrappers; no Buildroot compilation or target execution."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

TREE=Path(__file__).resolve().parents[1]
ROOT=TREE.parents[1]

class WifiTargets(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='wifi targets '); self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name).resolve(); self.out=self.root/'wifi output'; self.br=self.root/'build root';self.br.mkdir()
        self.cache=self.root/'source cache'
        checkout=os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')
        self.assertTrue(checkout,'NT98513_TEST_DIVINUS_CHECKOUT is required')
        subprocess.run(['python3',str(TREE/'tools/prepare_divinus_source.py'),'--checkout',checkout,'--output-dir',str(self.cache)],capture_output=True,check=True)
        (self.br/'Makefile').write_text("%:\n\t@printf '%s\\n' '$@' '$(O)' '$(NT98513_DIVINUS_SOURCE_DIR)' >> calls\n\t@case '$@' in *defconfig) mkdir -p '$(O)'; cp '$(BR2_EXTERNAL)/configs/$@' '$(O)/.config';; esac\n")

    def make(self,target,**extra):
        values={'NT98513_BUILDROOT':str(self.br),'NT98513_OUTPUT':str(self.out),'NT98513_DIVINUS_SOURCE_DIR':str(self.cache)}|extra
        return subprocess.run(['make','--no-print-directory','--silent','-C',str(ROOT),target]+
                              [key+'='+value for key,value in values.items()],capture_output=True,text=True)

    def test_all_target_routes_and_cross_variant_refusals(self):
        calls=self.br/'calls'
        for target,tail in [('nt98513-wifi-defconfig',[]),('nt98513-wifi-source',['source']),('nt98513-wifi',['all'])]:
            if calls.exists(): calls.unlink()
            result=self.make(target);self.assertEqual(result.returncode,0,result.stderr)
            expected=[]
            for goal in ['nt98513_wifi_defconfig']+tail: expected += [goal,str(self.out),str(self.cache)]
            self.assertEqual(calls.read_text().splitlines(),expected)
        config=(self.out/'.config').read_text(); calls.unlink()
        for target in ('ram','owned-input','ethernet-owned-input','ethernet','shared-clock','usb-host'):
            result=self.make('nt98513-'+target+'-defconfig');self.assertNotEqual(result.returncode,0,target)
            self.assertFalse(calls.exists());self.assertEqual((self.out/'.config').read_text(),config)
        for old in ('ram','shared_clock','ethernet','owned_input','ethernet_owned_input','usb_host'):
            config=(TREE/('configs/nt98513_'+old+'_defconfig')).read_text();(self.out/'.config').write_text(config)
            result=self.make('nt98513-wifi');self.assertNotEqual(result.returncode,0,old)
            self.assertFalse(calls.exists());self.assertEqual((self.out/'.config').read_text(),config)

    def test_fixed_stage_enforces_complete_eight_mib_boundary(self):
        self.out.mkdir();(self.out/'.config').write_text((TREE/'configs/nt98513_wifi_defconfig').read_text())
        linux=self.out/'build/linux-custom';linux.mkdir(parents=True);images=self.out/'images';images.mkdir()
        def payload(size):
            raw=b'\0'*size;ident=b'\x7fELF\x01\x01\x01'+b'\0'*9;load=0x80101000
            header=struct.pack('<16sHHIIIIIHHHHHH',ident,2,8,1,load+4,52,0,0x70001000,52,32,1,0,0,0)
            segment=struct.pack('<8I',1,0x100,load,load,size,size+64,5,0x100)
            (linux/'vmlinux').write_bytes((header+segment).ljust(0x100,b'\0')+raw)
            (images/'vmlinux.bin').write_bytes(raw)
        payload(0x800000-64)
        result=self.make('nt98513-wifi-image',NT98513_RAM_TIMESTAMP='0',NT98513_RAM_STAGE='0x03000000')
        self.assertEqual(result.returncode,0,result.stderr)
        report=json.loads(result.stdout.split('\n',1)[1]);self.assertEqual(report['image']['bytes'],0x800000)
        self.assertEqual(report['staging_physical_span'],[0x02c00000,0x03400000])
        image=images/'nt98513-wifi.uImage';saved=image.read_bytes()
        payload(0x800000-63)
        result=self.make('nt98513-wifi-image',NT98513_RAM_TIMESTAMP='0',NT98513_WIFI_IMAGE=str(images/'too-large.uImage'))
        self.assertNotEqual(result.returncode,0);self.assertIn('complete staging span',result.stderr)
        self.assertFalse((images/'too-large.uImage').exists());self.assertEqual(image.read_bytes(),saved)
        self.assertFalse((self.br/'calls').exists())

if __name__=='__main__':unittest.main()
