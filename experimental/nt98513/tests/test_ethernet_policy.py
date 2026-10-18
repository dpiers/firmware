#!/usr/bin/env python3
"""Execute the production policy added by patch5; no kernel or hardware needed."""
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
PATCH = HERE.parent / 'patches/linux/0005-nt98513-ethernet.patch'

def added_file(name):
    marker = '+++ b/drivers/net/ethernet/stmicro/stmmac/' + name + '\n'
    section = PATCH.read_text().split(marker, 1)[1].split('\n--- ', 1)[0]
    return ''.join(line[1:] for line in section.splitlines(keepends=True)
                   if line.startswith('+') and not line.startswith('+++'))

class EthernetPolicyTest(unittest.TestCase):
    def test_production_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in ('dwmac-nt98513-policy.h', 'stmmac-csr-policy.h'):
                (root / name).write_text(added_file(name))
            exe = root / 'policy'
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                            '-I', str(root), str(HERE / 'ethernet_policy.c'),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    unittest.main()
