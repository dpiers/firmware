"""Real target routing and refusal before the Buildroot boundary."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

TREE = Path(__file__).resolve().parents[1]
ROOT = TREE.parents[1]
CHECKOUT = os.environ.get('NT98513_TEST_DIVINUS_CHECKOUT')


class CombinedTargets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CHECKOUT:
            raise AssertionError('NT98513_TEST_DIVINUS_CHECKOUT is required')
        cls.temp = tempfile.TemporaryDirectory(prefix='nt98513-combined-source-')
        cls.cache = Path(cls.temp.name).resolve() / 'source cache'
        result = subprocess.run(['python3', str(TREE/'tools/prepare_divinus_source.py'),
            '--checkout', CHECKOUT, '--output-dir', str(cls.cache)], capture_output=True, text=True)
        if result.returncode:
            cls.temp.cleanup()
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nt98513-combined-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.br = self.root/'recording buildroot'; self.br.mkdir()
        self.out = self.root/'build output'
        self.log = self.br/'calls.txt'
        (self.br/'Makefile').write_text("%:\n\t@printf '%s\\n' '$@' '$(O)' '$(BR2_EXTERNAL)' '$(NT98513_DIVINUS_SOURCE_DIR)' >> calls.txt\n\t@case '$@' in *defconfig) mkdir -p '$(O)'; cp '$(BR2_EXTERNAL)/configs/$@' '$(O)/.config';; esac\n")

    def make(self, target, output=None, cache=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith('NT98513_')
               and k not in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'BOARD')}
        return subprocess.run(['make', '--no-print-directory', '--silent', '-C', str(ROOT), target,
            'NT98513_BUILDROOT='+str(self.br), 'NT98513_OUTPUT='+str(output or self.out),
            'NT98513_DIVINUS_SOURCE_DIR='+str(cache or self.cache)],
            capture_output=True, text=True, env=env, timeout=20)

    def calls(self):
        lines = self.log.read_text().splitlines() if self.log.exists() else []
        return [lines[n:n+4] for n in range(0, len(lines), 4)]

    def config(self, combined=True):
        # Explicit fixtures represent saved Buildroot identities, independently
        # of the production guard. Other resolved symbols may also be present.
        fragment = ('shared-clock.config $(BR2_EXTERNAL_NT98513_PATH)/board/ethernet.config'
                    if combined else 'owned-input-network.config')
        return ('BR2_LINUX_KERNEL=y\nBR2_STATIC_LIBS=y\n'
            'BR2_PACKAGE_DIVINUS_OWNED_NT98513=y\n'
            'BR2_LINUX_KERNEL_CUSTOM_CONFIG_FILE="$(BR2_EXTERNAL_NT98513_PATH)/board/linux.config"\n'
            'BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_NT98513_PATH)/board/'+fragment+'"\n'
            'BR2_PACKAGE_BUSYBOX_CONFIG_FRAGMENT_FILES="$(BR2_EXTERNAL_NT98513_PATH)/board/owned-input-network-busybox.config"\n')

    def test_combined_goals_forward_spaced_paths_and_allow_repeat(self):
        for suffix, goals in [('-defconfig', ['nt98513_ethernet_owned_input_defconfig']),
                              ('-source', ['nt98513_ethernet_owned_input_defconfig', 'source']),
                              ('', ['nt98513_ethernet_owned_input_defconfig', 'all'])]:
            if self.log.exists(): self.log.unlink()
            result = self.make('nt98513-ethernet-owned-input'+suffix)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.calls(), [[g, str(self.out), str(TREE), str(self.cache)] for g in goals])

    def test_old_owned_input_refuses_combined_output_before_make(self):
        self.out.mkdir(); config = self.config(); (self.out/'.config').write_text(config)
        for suffix in ('-defconfig', '-source', ''):
            result = self.make('nt98513-owned-input'+suffix)
            self.assertNotEqual(result.returncode, 0, result.stdout+result.stderr)
            self.assertEqual(self.calls(), [])
            self.assertEqual((self.out/'.config').read_text(), config)

    def test_combined_refuses_every_other_variant_before_make(self):
        self.out.mkdir()
        for name in ('ram', 'owned_input', 'shared_clock', 'ethernet'):
            config = (TREE/('configs/nt98513_'+name+'_defconfig')).read_text()
            (self.out/'.config').write_text(config)
            for suffix in ('-defconfig', '-source', ''):
                result = self.make('nt98513-ethernet-owned-input'+suffix)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.calls(), [])
                self.assertEqual((self.out/'.config').read_text(), config)

    def test_both_consumers_refuse_malformed_duplicate_and_symlink_outputs(self):
        self.out.mkdir()
        for combined, target in [(False, 'nt98513-owned-input'), (True, 'nt98513-ethernet-owned-input')]:
            valid = self.config(combined)
            for bad in [valid+'BR2_STATIC_LIBS=y\n', valid+'# BR2_STATIC_LIBS is not set\n',
                        valid+'BR2_BROKEN\n', valid.replace('BR2_STATIC_LIBS=y', 'BR2_STATIC_LIBS=n'),
                        valid.replace('BR2_LINUX_KERNEL=y', 'BR2_LINUX_KERNEL="y'),
                        valid.replace('BR2_PACKAGE_DIVINUS_OWNED_NT98513=y', '')]:
                (self.out/'.config').write_text(bad)
                result = self.make(target)
                self.assertNotEqual(result.returncode, 0, bad)
                self.assertEqual(self.calls(), [])
                self.assertEqual((self.out/'.config').read_text(), bad)
            (self.out/'.config').unlink()
            victim = self.root/'saved config'; victim.write_text(valid)
            (self.out/'.config').symlink_to(victim)
            self.assertNotEqual(self.make(target).returncode, 0)
            self.assertEqual(victim.read_text(), valid)
            (self.out/'.config').unlink()
            alias = self.root/'alias'; alias.symlink_to(self.out, target_is_directory=True)
            self.assertNotEqual(self.make(target, output=alias).returncode, 0)
            alias.unlink()
            self.assertNotEqual(self.make(target, output='relative').returncode, 0)
            self.assertNotEqual(self.make(target, output=str(self.root)+'/x/../build output').returncode, 0)
            self.assertEqual(self.calls(), [])

    def test_empty_and_expanded_matching_outputs_are_admitted(self):
        self.out.mkdir()
        result = self.make('nt98513-ethernet-owned-input-defconfig')
        self.assertEqual(result.returncode, 0, result.stderr)
        (self.out/'.config').write_text(self.config().replace('$(BR2_EXTERNAL_NT98513_PATH)', str(TREE)))
        result = self.make('nt98513-ethernet-owned-input-defconfig')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unknown_output_and_bad_cache_preserve_existing_data(self):
        self.out.mkdir(); marker = self.out/'keep'; marker.write_text('untouched')
        self.assertNotEqual(self.make('nt98513-ethernet-owned-input').returncode, 0)
        marker.unlink()
        bad = self.root/'bad cache'; bad.mkdir()
        manifest = json.loads((TREE/'package/divinus-owned-nt98513/source-manifest.json').read_text())
        archive = bad/manifest['archive']; archive.write_bytes(b'wrong archive')
        self.assertNotEqual(self.make('nt98513-ethernet-owned-input', cache=bad).returncode, 0)
        self.assertEqual(archive.read_bytes(), b'wrong archive')
        self.assertEqual(self.calls(), [])
        self.assertEqual(hashlib.sha256((self.cache/manifest['archive']).read_bytes()).hexdigest(), manifest['sha256'])


if __name__ == '__main__':
    unittest.main()
