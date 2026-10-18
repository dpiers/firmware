#!/usr/bin/env python3
"""Compile exact patch6 production helpers and the accepted pre-change path."""
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
PATCH = HERE.parent / 'patches/linux/0006-nt98513-phy-reset.patch'

def added_file(name):
    marker = '+++ b/drivers/net/ethernet/stmicro/stmmac/' + name + '\n'
    section = PATCH.read_text().split(marker, 1)[1].split('\n--- ', 1)[0]
    return ''.join(line[1:] for line in section.splitlines(keepends=True)
                   if line.startswith('+') and not line.startswith('+++'))

class PhyResetTest(unittest.TestCase):
    def run_fixture(self, baseline=False, scenario=None):
        self.assertTrue(PATCH.is_file(), 'selected-PHY reset implementation is missing')
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in ('dwmac-nt98513-phy.h', 'stmmac-mdio-setup.h'):
                code = added_file(name)
                if baseline and name == 'stmmac-mdio-setup.h':
                    code = (HERE / 'phy_registration_before.h').read_text()
                (root / name).write_text(code)
            exe = root / 'phy-reset'
            flags = ['-Wno-unused-function'] if baseline else []
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', *flags,
                            '-I', str(root), str(HERE / 'phy_reset.c'),
                            '-o', str(exe)], check=True)
            return subprocess.run([str(exe), *([scenario] if scenario else [])],
                                  capture_output=True, text=True)

    def test_baseline_reproduces_missing_reset_error_propagation(self):
        result = self.run_fixture(baseline=True, scenario='reset-failure')
        self.assertEqual(result.returncode, 1)
        self.assertIn('ret=0 published=1 unregister=0 free=0', result.stderr)

    def test_production_reset_and_registration_paths(self):
        result = self.run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('all assertions passed', result.stdout)
        result = self.run_fixture(scenario='reset-failure')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_glue_and_core_call_tested_helpers(self):
        patch = PATCH.read_text()
        self.assertIn('+\tplat->mdio_phy_setup = nt98513_phy_reset;', patch)
        self.assertIn('+\treturn stmmac_mdio_finish_registration(priv, new_bus);', patch)

if __name__ == '__main__':
    unittest.main()
