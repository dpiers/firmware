"""Run the exact USB policy supplied to the kernel, using inert MMIO."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
PATCH = HERE.parent/'patches/linux/0008-nt98513-usb-host.patch'

class USBHostPolicy(unittest.TestCase):
    def test_production_policy(self):
        self.assertTrue(PATCH.is_file(), 'USB host production patch is not implemented')
        text = PATCH.read_text()
        marker = '+++ b/drivers/usb/fotg210/nt98513-policy.h\n'
        self.assertIn(marker, text)
        section = text.split(marker, 1)[1].split('\n--- ', 1)[0]
        code = ''.join(line[1:] for line in section.splitlines(keepends=True)
                       if line.startswith('+') and not line.startswith('+++'))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); (root/'nt98513-policy.h').write_text(code)
            exe = root/'policy'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I', str(root), str(HERE/'usb_host_policy.c'), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_actual_lifecycle_failures(self):
        text=PATCH.read_text()
        def additions(path):
            section=text.split('+++ b/'+path+'\n',1)[1].split('\n--- ',1)[0]
            return ''.join(line[1:] for line in section.splitlines(keepends=True)
                           if line.startswith('+') and not line.startswith('+++'))
        source=additions('drivers/usb/fotg210/fotg210-hcd.c')
        functions=[]
        for signature in ('static int nt98513_require_halt(', 'static void nt98513_notify_death(', 'static void nt98513_hcd_stop(',
                          'static int nt98513_hcd_setup(', 'static int nt98513_hcd_run(',
                          'int fotg210_nt98513_probe('):
            start=source.index(signature); opening=source.index('{',start);depth=1;end=opening+1
            while depth:
                depth += (source[end]=='{')-(source[end]=='}');end+=1
            functions.append(source[start:end]+'\n')
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'nt98513-policy.h').write_text(additions('drivers/usb/fotg210/nt98513-policy.h'))
            (root/'nt-lifecycle.inc').write_text('\n'.join(functions))
            exe=root/'lifecycle'
            subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root),
                str(HERE/'usb_host_lifecycle.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
            # Behavioral negative control: restoring unconditional core death
            # notification must queue work before successful registration and
            # fail the harness before its modeled root-hub invalidation.
            actual = (root / 'nt-lifecycle.inc').read_text()
            gate = 'if (nt->core_ready && !nt->death_notified)'
            self.assertEqual(actual.count(gate), 1)
            (root / 'nt-lifecycle.inc').write_text(actual.replace(
                gate, 'if (!nt->death_notified)'))
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                '-I', str(root), str(HERE / 'usb_host_lifecycle.c'), '-o', str(exe)], check=True)
            rejected = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn('!h->died_work', rejected.stderr)
            print('NT98513 USB negative control: ungated early death rejected')

if __name__ == '__main__':
    unittest.main()
