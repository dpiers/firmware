"""Compile the final patch stack's production owner/MMC/CC core with fake I/O."""
import os
import pathlib
import subprocess
import tempfile
import unittest

EXTERNAL = pathlib.Path(__file__).resolve().parents[1]


def materialize(root):
    # A file is eligible only after an actual patch added it. Apply every later
    # hunk to it, including patch4's replacement of the historical patch3 owner.
    for patch in sorted((EXTERNAL / 'patches/linux').glob('*.patch')):
        # Existing patches use both git-style and plain unified diffs.
        import re
        text = patch.read_text()
        starts = list(re.finditer(r'^--- (?:a/|/dev/null)', text, re.M))
        for i, start in enumerate(starts):
            end = starts[i + 1].start() if i + 1 < len(starts) else len(text)
            part = text[start.start():end]
            part = part.split('\ndiff --git ', 1)[0]
            old, new = part.splitlines()[:2]
            name = new[4:].split('\t')[0].removeprefix('b/')
            target = root / name
            if old != '--- /dev/null' and not target.exists():
                continue
            if not (name.startswith('drivers/clk/nt98513-shared/') or
                    name.startswith('drivers/soc/nt98513/') or
                    name.startswith('include/linux/soc/nt98513')):
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['patch', '-p1', '--batch', '--forward'], cwd=root,
                           input=part, text=True, capture_output=True, check=True)


def compile_case(root, output):
    stub = root / 'stubs/linux'
    stub.mkdir(parents=True)
    for name in ['device.h', 'err.h', 'init.h', 'io.h', 'iopoll.h', 'module.h',
                 'of.h', 'platform_device.h', 'slab.h', 'clk-provider.h',
                 'mutex.h', 'types.h', 'delay.h', 'of_clk.h']:
        (stub / name).write_text('/* kernel service supplied by native fixture */\n')
    command = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
               '-Werror', '-Wno-unused-parameter', '-Wno-unused-function',
               '-I' + str(root / 'stubs'), '-I' + str(root / 'include'),
               '-I' + str(root / 'drivers/clk/nt98513-shared/include'),
               '-I' + str(root / 'drivers/clk/nt98513-shared/driver'),
               '-I' + str(root / 'drivers/clk/nt98513-shared/core'),
               '-I' + str(root / 'drivers/soc/nt98513'),
               str(EXTERNAL / 'tests/mmc_clock.c'), '-o', str(output)]
    return subprocess.run(command, text=True, capture_output=True)


class ClockProduction(unittest.TestCase):
    def test_production_lifecycle_and_faults(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            materialize(root)
            result = compile_case(root, root / 'test')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(root / 'test')], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('production MMC clock checks passed', result.stdout)

    def test_behavioral_rejection_controls(self):
        mutations = [
            ('pulse missing first delay', 'udelay(10); /* outside bank */', '; /* removed delay */'),
            ('resume repeats prepare pulse', 'if (first) {', 'if (1) {'),
            ('divider rounds down', '(192000000U + requested - 1) / requested', '192000000U / requested'),
            ('instance1 phase corrupted', 'if (!c->index)', 'if (1)'),
            ('owned state drift ignored', 'error = mmc_validate(c, admission);', 'error = 0;'),
            ('gate-off omitted', 'error = mmc_set(c, MMC_GATE, 0);', 'error = 0;'),
            ('release failure ignored', 'nt_cc_leave(c->owner->bank);\n return mmc_latch(c, error ? error : released);', 'nt_cc_leave(c->owner->bank);\n (void)released; return mmc_latch(c, error);'),
        ]
        for label, old, new in mutations:
            with self.subTest(label=label), tempfile.TemporaryDirectory() as td:
                root = pathlib.Path(td)
                materialize(root)
                source = root / 'drivers/clk/nt98513-shared/driver/nt98513_mmc_clock.c'
                text = source.read_text()
                self.assertIn(old, text)
                source.write_text(text.replace(old, new, 1))
                result = compile_case(root, root / 'test')
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(root / 'test')], capture_output=True)
                self.assertNotEqual(result.returncode, 0, label)


if __name__ == '__main__':
    unittest.main()
