#!/usr/bin/env python3
"""Compile exact accepted functions, applying the shipped main.c patch hunks."""
import pathlib
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
PATCH = HERE.parent / 'patches/linux/0007-nt98513-dma-irq-order.patch'
MAIN = 'drivers/net/ethernet/stmicro/stmmac/stmmac_main.c'


def production_functions(baseline=False):
    source = (HERE / 'dma_irq_before.c').read_text()
    if baseline:
        return source
    section = PATCH.read_text().split('+++ b/' + MAIN + '\n', 1)[1]
    section = section.split('\n--- a/', 1)[0]
    for hunk in ('\n' + section).split('\n@@')[1:]:
        lines = hunk.splitlines(keepends=True)[1:]
        old = ''.join(line[1:] for line in lines if line.startswith((' ', '-')))
        new = ''.join(line[1:] for line in lines if line.startswith((' ', '+')))
        if source.count(old) != 1:
            raise AssertionError('patch hunk does not match exact accepted functions')
        source = source.replace(old, new, 1)
    return source


def run_fixture(baseline=False, scenario=None):
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / 'dma_irq_functions.c').write_text(production_functions(baseline))
        exe = root / 'dma-irq'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-variable', '-Wno-unused-parameter',
                        '-Wno-unused-but-set-variable', '-I', str(root),
                        str(HERE / 'dma_irq.c'), '-o', str(exe)], check=True)
        return subprocess.run([str(exe), *([scenario] if scenario else [])],
                              capture_output=True, text=True)


class DmaIrqOrderTest(unittest.TestCase):
    def test_baseline_irq_failure_starts_dma_before_freeing_rings(self):
        result = run_fixture(True, 'irq-failure')
        self.assertEqual(result.returncode, 1)
        self.assertIn('events=HDIF starts=1 requests=1 frees=1', result.stderr)

    def test_production_open_xdp_resume_and_reopen(self):
        result = run_fixture()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('all DMA/IRQ production-path assertions passed', result.stdout)

    def test_policy_is_selected_by_nt98513_glue(self):
        patch = PATCH.read_text()
        self.assertIn('+\tplat->dma_start_after_irq = true;', patch)
        self.assertIn('+\tbool dma_start_after_irq;', patch)


if __name__ == '__main__':
    unittest.main()
