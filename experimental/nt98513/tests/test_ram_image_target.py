#!/usr/bin/env python3
"""Make-target integration tests; tiny ELF fixtures are not hardware evidence."""
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[3]
LOAD = 0x80101000


class RamImageTargetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='nt98513 image target ')
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / 'build output'
        self.linux = self.output / 'build/linux-custom'
        self.images = self.output / 'images'
        self.linux.mkdir(parents=True)
        self.images.mkdir()
        self.raw = b'\0' * 64
        ident = b'\x7fELF\x01\x01\x01' + b'\0' * 9
        header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 8, 1,
                             LOAD + 4, 52, 0, 0x70001000, 52, 32, 1, 0, 0, 0)
        segment = struct.pack('<8I', 1, 0x100, LOAD, LOAD, 64, 128, 5, 0x100)
        self.elf = (header + segment).ljust(0x100, b'\0') + self.raw
        (self.linux / 'vmlinux').write_bytes(self.elf)
        (self.images / 'vmlinux.bin').write_bytes(self.raw)
        self.image = self.images / 'nt98513-ram.uImage'

    def run_make(self, omit=(), **changes):
        values = {
            'NT98513_OUTPUT': str(self.output),
            'NT98513_RAM_TIMESTAMP': '1791936000',
            'NT98513_RAM_STAGE': '0x03000000',
        }
        values.update(changes)
        for key in omit:
            values.pop(key)
        env = os.environ.copy()
        for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'BOARD'):
            env.pop(key, None)
        for key in list(env):
            if key.startswith('NT98513_'):
                env.pop(key)
        return subprocess.run(
            ['make', '--no-print-directory', '--silent', '-C', str(ROOT),
             'nt98513-ram-image'] + [k + '=' + v for k, v in values.items()],
            capture_output=True, text=True, env=env, timeout=15)

    def test_target_packages_completed_output_without_buildroot(self):
        result = self.run_make()
        self.assertEqual(result.returncode, 0, result.stderr)
        image = self.image.read_bytes()
        h = struct.unpack('>7I4B32s', image[:64])
        self.assertEqual(h[0], 0x27051956)
        self.assertEqual(h[2:6], (1791936000, 64, LOAD, LOAD + 4))
        self.assertEqual(h[7:11], (5, 5, 2, 0))
        self.assertEqual(image[64:], self.raw)
        header = image[:4] + b'\0' * 4 + image[8:64]
        self.assertEqual(h[1], zlib.crc32(header))
        self.assertEqual(h[6], zlib.crc32(self.raw))
        report = json.loads(result.stdout)
        self.assertEqual(report['output'], str(self.image.resolve()))
        self.assertEqual(report['staging_physical_span'], [0x3000000, 0x3000080])
        self.assertFalse(report['hardware_validated'])
        self.assertEqual((self.linux / 'vmlinux').read_bytes(), self.elf)
        self.assertEqual((self.images / 'vmlinux.bin').read_bytes(), self.raw)

    def test_explicit_output_timestamp_and_alias_are_forwarded(self):
        image = Path(self.temp.name) / 'chosen image.uImage'
        result = self.run_make(NT98513_RAM_IMAGE=str(image),
                               NT98513_RAM_TIMESTAMP='0', NT98513_RAM_STAGE='0xa3000000')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['timestamp'], 0)
        self.assertEqual(report['staging_address'], 0xa3000000)
        self.assertEqual(report['output'], str(image.resolve()))
        self.assertTrue(image.is_file())
        self.assertFalse(self.image.exists())

    def test_required_inputs_have_no_implicit_defaults(self):
        for name in ('NT98513_OUTPUT', 'NT98513_RAM_TIMESTAMP', 'NT98513_RAM_STAGE'):
            with self.subTest(name=name):
                result = self.run_make(omit=(name,))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Set ' + name, result.stderr)
                self.assertFalse(self.image.exists())

    def test_existing_output_and_symlink_are_preserved(self):
        self.image.write_bytes(b'preserve existing image')
        result = self.run_make()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RAM image refused', result.stderr)
        self.assertEqual(self.image.read_bytes(), b'preserve existing image')
        link = Path(self.temp.name) / 'existing symlink'
        link.symlink_to(self.image)
        result = self.run_make(NT98513_RAM_IMAGE=str(link))
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(link.is_symlink())
        self.assertEqual(self.image.read_bytes(), b'preserve existing image')

    def test_bad_raw_pair_fails_without_output(self):
        (self.images / 'vmlinux.bin').write_bytes(b'wrong')
        result = self.run_make()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('raw bytes differ', result.stderr)
        self.assertFalse(self.image.exists())

    def test_missing_elf_fails_without_output(self):
        (self.linux / 'vmlinux').unlink()
        result = self.run_make()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('RAM image refused', result.stderr)
        self.assertFalse(self.image.exists())

    def test_invalid_timestamp_and_overlapping_staging_fail(self):
        for args in ({'NT98513_RAM_TIMESTAMP': '-1'},
                     {'NT98513_RAM_STAGE': '0x80101000'}):
            with self.subTest(args=args):
                result = self.run_make(**args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('RAM image refused', result.stderr)
                self.assertFalse(self.image.exists())


if __name__ == '__main__':
    unittest.main()
