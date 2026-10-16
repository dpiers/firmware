#!/usr/bin/env python3
"""Exercise real firmware Make targets; the recorder does not build firmware."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
TREE = ROOT / 'experimental/nt98513'
CHECKOUT = os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')


class OwnedTargets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CHECKOUT:
            raise AssertionError('NT98513_TEST_DIVINUS_CHECKOUT is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='nt98513-target-source-')
        cls.cache = Path(cls.temp.name).resolve() / 'cache'
        result = subprocess.run(['python3', str(TREE / 'tools/prepare_divinus_source.py'), '--checkout', CHECKOUT, '--output-dir', str(cls.cache)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='nt98513-target-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.br = self.root / 'recording-buildroot'; self.br.mkdir()
        self.output = self.root / 'owned-output'
        self.log = self.br / 'calls.txt'
        (self.br / 'Makefile').write_text('''%:
	@printf '%s\\n' '$@' '$(O)' '$(BR2_EXTERNAL)' '$(NT98513_DIVINUS_SOURCE_DIR)' >> calls.txt
	@case '$@' in *defconfig) mkdir -p '$(O)'; printf '%s\\n' 'BR2_PACKAGE_DIVINUS_OWNED_NT98513=y' > '$(O)/.config';; esac
''')

    def make(self, target, omit=(), **changes):
        variables = {'NT98513_BUILDROOT': str(self.br), 'NT98513_OUTPUT': str(self.output), 'NT98513_DIVINUS_SOURCE_DIR': str(self.cache)}
        variables.update(changes)
        for key in omit:
            variables.pop(key, None)
        env = os.environ.copy()
        for key in list(env):
            if key.startswith('NT98513_') or key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'BOARD'):
                env.pop(key)
        return subprocess.run(['make', '--no-print-directory', '--silent', '-C', str(ROOT), target] + [key + '=' + value for key, value in variables.items()], env=env, capture_output=True, text=True, timeout=20)

    def calls(self):
        lines = self.log.read_text().splitlines() if self.log.exists() else []
        return [lines[n:n+4] for n in range(0, len(lines), 4)]

    def test_enabled_targets_forward_cache_and_output_on_every_call(self):
        for target, goals in [('nt98513-owned-input-defconfig', ['nt98513_owned_input_defconfig']), ('nt98513-owned-input-source', ['nt98513_owned_input_defconfig', 'source']), ('nt98513-owned-input', ['nt98513_owned_input_defconfig', 'all'])]:
            with self.subTest(target=target):
                if self.log.exists(): self.log.unlink()
                result = self.make(target)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.calls(), [[goal, str(self.output), str(TREE), str(self.cache)] for goal in goals])

    def test_missing_wrong_cache_and_console_output_fail_before_buildroot(self):
        for changes in ({'NT98513_DIVINUS_SOURCE_DIR': ''}, {'NT98513_DIVINUS_SOURCE_DIR': str(self.root / 'missing')}, {'NT98513_DIVINUS_SOURCE_DIR': 'relative/cache'}):
            result = self.make('nt98513-owned-input', **changes)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.calls(), [])
        self.output.mkdir(); (self.output / '.config').write_text('# console\n')
        result = self.make('nt98513-owned-input')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual((self.output / '.config').read_text(), '# console\n')

    def test_console_target_needs_no_divinus_variables(self):
        result = self.make('nt98513-ram-defconfig', omit=('NT98513_DIVINUS_SOURCE_DIR',))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [['nt98513_ram_defconfig', str(self.output), str(TREE), '']])
        config = (TREE / 'configs/nt98513_ram_defconfig').read_bytes()
        self.assertEqual(hashlib.sha256(config).hexdigest(), 'f47acb48062cdcefeed31e479735b37fea62a4c37b8b8596a8a455f78bfe5b56')
        owned = (TREE / 'configs/nt98513_owned_input_defconfig').read_bytes()
        self.assertEqual(owned[:len(config)], config)
        self.assertEqual(len(owned[len(config):].decode().strip().splitlines()), 3)

    def test_prepare_target_runs_without_buildroot_or_output(self):
        cache = self.root / 'prepared'
        result = self.make('nt98513-owned-input-prepare-source', omit=('NT98513_BUILDROOT', 'NT98513_OUTPUT'), NT98513_DIVINUS_CHECKOUT=CHECKOUT, NT98513_DIVINUS_SOURCE_DIR=str(cache))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.calls(), [])
        m = json.loads((TREE / 'package/divinus-owned-nt98513/source-manifest.json').read_text())
        self.assertEqual(hashlib.sha256((cache / m['archive']).read_bytes()).hexdigest(), m['sha256'])

    def test_package_parse_requires_cache_only_when_enabled(self):
        package = TREE / 'package/divinus-owned-nt98513/divinus-owned-nt98513.mk'
        fixture = self.root / 'package.mk'
        fixture.write_text('include ' + str(package) + '\nall:\n\t@printf "%s\\n" "$(DIVINUS_OWNED_NT98513_SITE)"\n')
        base = ['make', '--no-print-directory', '--silent', '-f', str(fixture)]
        disabled = subprocess.run(base, capture_output=True, text=True)
        self.assertEqual(disabled.returncode, 0, disabled.stderr)
        enabled = subprocess.run(base + ['BR2_PACKAGE_DIVINUS_OWNED_NT98513=y'], capture_output=True, text=True)
        self.assertNotEqual(enabled.returncode, 0)
        good = subprocess.run(base + ['BR2_PACKAGE_DIVINUS_OWNED_NT98513=y', 'NT98513_DIVINUS_SOURCE_DIR=' + str(self.cache)], capture_output=True, text=True)
        self.assertEqual(good.returncode, 0, good.stderr)
        self.assertEqual(good.stdout.strip(), 'file://' + str(self.cache))


if __name__ == '__main__':
    unittest.main()
