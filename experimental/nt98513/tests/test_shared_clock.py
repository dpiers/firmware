import unittest, subprocess, tempfile, pathlib, itertools, os
EXTERNAL=pathlib.Path(__file__).resolve().parents[1]
TEMP=tempfile.TemporaryDirectory()
K=pathlib.Path(TEMP.name)
# Materialize only complete added files from the actual distributed patch.
lines=(EXTERNAL/'patches/linux/0003-nt98513-shared-clock-board.patch').read_text().splitlines(True)
i=0
while i < len(lines):
 if lines[i].startswith('--- '):
  new=lines[i]=='--- /dev/null\n'; target=lines[i+1][6:].strip();i+=2;body=[]
  while i<len(lines) and not lines[i].startswith('--- '):
   if new and lines[i].startswith('+'):body.append(lines[i][1:])
   i+=1
  if new:
   p=K/target;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(''.join(body))
 else:i+=1
GOOD={'MACH_NT98513':'y','OF':'y','COMMON_CLK':'y','WATCHDOG':'y','WATCHDOG_HANDLE_BOOT_ENABLED':'y','WATCHDOG_NOWAYOUT':'y','WATCHDOG_OPEN_TIMEOUT':'0','NT98513_SHARED_CLOCK_BOARD':'y','NT98513_SHARED_CLOCK':'y','NT98513_WATCHDOG_SHARED':'y'}
TRIO=['NT98513_SHARED_CLOCK_BOARD','NT98513_SHARED_CLOCK','NT98513_WATCHDOG_SHARED']
class Guards(unittest.TestCase):
 def run_raw(self,c,expect,extra=''):
  with tempfile.TemporaryDirectory() as td:
   p=pathlib.Path(td)/'config';p.write_text(''.join('CONFIG_%s=%s\n'%x for x in c.items())+extra)
   r=subprocess.run(['sh',str(K/'arch/mips/nt98513/check-clock-config.sh'),str(p)],capture_output=True,text=True)
   self.assertEqual(r.returncode==0,expect,(c,r.stderr))
 def test_valid_and_base(self):
  self.run_raw(GOOD,True);self.run_raw({},True);self.run_raw({'NT98513_WATCHDOG':'y'},True)
 def test_partial_requests(self):
  for vals in itertools.product(['n','y','m'],repeat=3):
   c=GOOD|dict(zip(TRIO,vals));self.run_raw(c,vals==('y','y','y') or vals==('n','n','n'))
 def test_constraints(self):
  for n in ['MACH_NT98513','OF','COMMON_CLK','WATCHDOG','WATCHDOG_HANDLE_BOOT_ENABLED','WATCHDOG_NOWAYOUT']:
   self.run_raw(GOOD|{n:'n'},False)
  for n in ['NT98513_WATCHDOG','SMP','SUSPEND','HIBERNATION','OF_DYNAMIC']:
   self.run_raw(GOOD|{n:'y'},False)
  for x in ['1','-1','00','"0"','m']:self.run_raw(GOOD|{'WATCHDOG_OPEN_TIMEOUT':x},False)
  self.run_raw(GOOD,False,'CONFIG_WATCHDOG_NOWAYOUT=y\n')
 def test_duplicate_cannot_hide_request(self):
  self.run_raw({},False,'CONFIG_NT98513_SHARED_CLOCK=y\nCONFIG_NT98513_SHARED_CLOCK=n\n')
 def test_resolved(self):
  for c,want in [(GOOD,True),({},True),(GOOD|{'COMMON_CLK':'n'},False),(GOOD|{'NT98513_SHARED_CLOCK_BOARD':'n'},False),(GOOD|{'WATCHDOG_OPEN_TIMEOUT':'1'},False)]:
   with tempfile.TemporaryDirectory() as td:
    p=pathlib.Path(td)/'Makefile';p.write_text(''.join('CONFIG_%s := %s\n'%x for x in c.items())+'include '+str((K/'arch/mips/nt98513/clock-config-guard.mk').resolve())+'\nall:\n\t@true\n')
    r=subprocess.run(['make','-f',str(p)],capture_output=True,text=True);self.assertEqual(r.returncode==0,want,r.stderr)
class Publication(unittest.TestCase):
 def test_imported_bytes(self):
  import hashlib,json
  manifest=json.loads((EXTERNAL/'shared-clock-source-manifest.json').read_text())
  for row in manifest['files']:
   self.assertEqual(hashlib.sha256((K/row['kernel']).read_bytes()).hexdigest(),row['sha256'],row['kernel'])
 def test_request_hook_and_expected_variant(self):
  with tempfile.TemporaryDirectory() as td:
   d=pathlib.Path(td);p=d/'requested.config'
   good=''.join('CONFIG_%s=%s\n'%x for x in GOOD.items())
   p.write_text(good.replace('CONFIG_WATCHDOG_NOWAYOUT=y','CONFIG_WATCHDOG_NOWAYOUT=n'))
   (d/'Makefile').write_text('srctree := '+str(K)+'\nCONFIG_SHELL := /bin/sh\nneed-config := 1\nKCONFIG_CONFIG := requested.config\ninclude $(srctree)/arch/mips/nt98513/clock-request-guard.mk\nall olddefconfig syncconfig menuconfig defconfig board_defconfig help clean:\n\t@true\n')
   for target in ['all','olddefconfig','syncconfig','menuconfig']:
    self.assertNotEqual(subprocess.run(['make',target],cwd=d,capture_output=True).returncode,0)
   for args in [['defconfig'],['board_defconfig'],['help','need-config='],['clean','need-config=']]:
    self.assertEqual(subprocess.run(['make']+args,cwd=d,capture_output=True).returncode,0)
   p.write_text(good)
   self.assertEqual(subprocess.run(['make','olddefconfig'],cwd=d,capture_output=True).returncode,0)
   # Caller must reject a silent shared-to-base normalization.
   (d/'.config').write_text('CONFIG_MACH_NT98513=y\n')
   self.assertNotEqual(subprocess.run(['sh',str(EXTERNAL/'tools/check_shared_clock_kernel.sh'),str(d)],capture_output=True).returncode,0)


class OutputIsolation(unittest.TestCase):
 def test_refuse_existing_console_before_make(self):
  root=EXTERNAL.parents[1]
  with tempfile.TemporaryDirectory() as td:
   d=pathlib.Path(td).resolve();br=d/'br';br.mkdir();out=d/'output';out.mkdir();original='BR2_LINUX_KERNEL=y\n';(out/'.config').write_text(original)
   (br/'Makefile').write_text('%:\n\t@touch called\n\t@echo REPLACED > "$(O)/.config"\n')
   for target in ['nt98513-shared-clock-defconfig','nt98513-shared-clock-source','nt98513-shared-clock']:
    r=subprocess.run(['make','-C',str(root),target,'NT98513_BUILDROOT='+str(br),'NT98513_OUTPUT='+str(out)],capture_output=True,text=True)
    self.assertNotEqual(r.returncode,0);self.assertFalse((br/'called').exists());self.assertEqual((out/'.config').read_text(),original)

 def test_output_guard_states(self):
  with tempfile.TemporaryDirectory() as td:
   d=pathlib.Path(td).resolve();out=d/'out';tool=EXTERNAL/'tools/check_shared_clock_output.py'
   def invoke(want):
    r=subprocess.run(['python3',str(tool),str(out)],capture_output=True,text=True);self.assertEqual(r.returncode==0,want,r.stderr)
   for unsafe in ['', 'relative-output', str(d)+'/child/../out']:
    self.assertNotEqual(subprocess.run(['python3',str(tool),unsafe],capture_output=True).returncode,0)
   invoke(True);out.mkdir();invoke(True)
   (out/'other').write_text('x');invoke(False)
   valid='BR2_LINUX_KERNEL=y\nBR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="$(BR2_EXTERNAL_NT98513_PATH)/board/linux.config"\nBR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_NT98513_PATH)/board/shared-clock.config"\n'
   (out/'.config').write_text(valid);invoke(True)
   (out/'.config').write_text(valid+'BR2_PACKAGE_DIVINUS_OWNED_NT98513=y\n');invoke(False)
   (out/'.config').write_text(valid.replace('shared-clock.config','owned-input-network.config'));invoke(False)
   (out/'.config').unlink();(out/'.config').symlink_to(d/'nonexistent');invoke(False)
   alias=d/'alias';alias.symlink_to(out,target_is_directory=True)
   self.assertNotEqual(subprocess.run(['python3',str(tool),str(alias)],capture_output=True).returncode,0)

 def test_relative_output_cannot_check_different_directory(self):
  root=EXTERNAL.parents[1]
  with tempfile.TemporaryDirectory() as td:
   d=pathlib.Path(td).resolve();br=d/'br';br.mkdir();victim=br/'relative-output';victim.mkdir();original='BR2_LINUX_KERNEL=y\n';(victim/'.config').write_text(original)
   (br/'Makefile').write_text('%:\n\t@touch called\n\t@echo REPLACED > "$(O)/.config"\n')
   for target in ['nt98513-shared-clock-defconfig','nt98513-shared-clock-source','nt98513-shared-clock']:
    r=subprocess.run(['make','-C',str(root),target,'NT98513_BUILDROOT='+str(br),'NT98513_OUTPUT=relative-output'],capture_output=True,text=True)
    self.assertNotEqual(r.returncode,0);self.assertFalse((br/'called').exists());self.assertEqual((victim/'.config').read_text(),original)

if __name__=="__main__": unittest.main()
